//
//  AsusSMC.cpp
//  AsusSMC
//
//  Copyright © 2018-2020 Le Bao Hiep. All rights reserved.
//

#include "AsusSMC.hpp"

bool ADDPR(debugEnabled) = false;
uint32_t ADDPR(debugPrintDelay) = 0;

#define super IOService
OSDefineMetaClassAndStructors(AsusSMC, IOService)

bool AsusSMC::init(OSDictionary *dict) {
    if (!super::init(dict)) {
        return false;
    }

    _notificationServices = OSSet::withCapacity(1);

    kev.setVendorID("com.hieplpvip");
    kev.setEventCode(AsusSMCEventCode);

    atomic_init(&currentLux, 0);
    atomic_init(&currentFanSpeed, 0);

    lksbLock = IOLockAlloc();
    if (!lksbLock) {
        SYSLOG("lksb", "failed to allocate LKSB lock");
        return false;
    }

    return true;
}

IOService *AsusSMC::probe(IOService *provider, SInt32 *score) {
    if (!super::probe(provider, score)) {
        return NULL;
    }

    IOACPIPlatformDevice *dev = OSDynamicCast(IOACPIPlatformDevice, provider);
    if (!dev) {
        return NULL;
    }

    OSObject *obj;
    dev->evaluateObject("_UID", &obj);

    OSString *name = OSDynamicCast(OSString, obj);
    if (!name) {
        return NULL;
    }

    IOService *ret = NULL;
    if (name->isEqualTo("ATK")) {
        ret = this;
    }
    name->release();

    return ret;
}

bool AsusSMC::start(IOService *provider) {
    if (!provider || !super::start(provider)) {
        SYSLOG("atk", "failed to start parent");
        return false;
    }

    atkDevice = (IOACPIPlatformDevice *)provider;

    parse_WDG();

    initATKDevice();

    initALSDevice();

    initEC0Device();

    initBattery();

    initVirtualKeyboard();

    startATKDevice();

    workloop = getWorkLoop();
    if (!workloop) {
        DBGLOG("atk", "Failed to get workloop");
        return false;
    }
    workloop->retain();

    command_gate = IOCommandGate::commandGate(this);
    if (!command_gate || (workloop->addEventSource(command_gate) != kIOReturnSuccess)) {
        DBGLOG("atk", "Could not open command gate");
        return false;
    }

    setProperty("IsTouchpadEnabled", true);
    setProperty("Copyright", "Copyright © 2018-2020 Le Bao Hiep. All rights reserved.");

    extern kmod_info_t kmod_info;
    setProperty("AsusSMC-Version", kmod_info.version);
#ifdef DEBUG
    setProperty("AsusSMC-Build", "Debug");
#else
    setProperty("AsusSMC-Build", "Release");
#endif

    registerNotifications();

    registerVSMC();

    registerService();

    return true;
}

void AsusSMC::stop(IOService *provider) {
    if (poller) {
        poller->cancelTimeout();
    }
    if (workloop && poller) {
        workloop->removeEventSource(poller);
    }
    if (workloop && command_gate) {
        workloop->removeEventSource(command_gate);
    }
    OSSafeReleaseNULL(workloop);
    OSSafeReleaseNULL(poller);
    OSSafeReleaseNULL(command_gate);

    _publishNotify->remove();
    _terminateNotify->remove();
    _notificationServices->flushCollection();
    OSSafeReleaseNULL(_publishNotify);
    OSSafeReleaseNULL(_terminateNotify);
    OSSafeReleaseNULL(_notificationServices);

    OSSafeReleaseNULL(kbdDevice);

    IOLockFree(lksbLock);
    LKSBCallbacks.deinit();

    super::stop(provider);
    return;
}

IOReturn AsusSMC::message(uint32_t type, IOService *provider, void *argument) {
    switch (type) {
        case kIOACPIMessageDeviceNotification:
            if (directACPImessaging) {
                handleMessage(*((uint32_t *)argument));
            } else {
                uint32_t event = *((uint32_t *)argument);
                OSNumber *arg = OSNumber::withNumber(event, 32);
                uint32_t res;
                atkDevice->evaluateInteger("_WED", &res, (OSObject **)&arg, 1);
                arg->release();
                handleMessage(res);
            }
            break;
        default:
            DBGLOG("atk", "Unexpected message: %u Type %x Provider %s", *((uint32_t *)argument), type, provider->getName());
            break;
    }
    return kIOReturnSuccess;
}

int AsusSMC::wmi_parse_guid(const char *in, char *out) {
    for (int i = 3; i >= 0; i--) {
        out += snprintf(out, 3, "%02X", in[i] & 0xFF);
    }

    out += snprintf(out, 2, "-");
    out += snprintf(out, 3, "%02X", in[5] & 0xFF);
    out += snprintf(out, 3, "%02X", in[4] & 0xFF);
    out += snprintf(out, 2, "-");
    out += snprintf(out, 3, "%02X", in[7] & 0xFF);
    out += snprintf(out, 3, "%02X", in[6] & 0xFF);
    out += snprintf(out, 2, "-");
    out += snprintf(out, 3, "%02X", in[8] & 0xFF);
    out += snprintf(out, 3, "%02X", in[9] & 0xFF);
    out += snprintf(out, 2, "-");

    for (int i = 10; i <= 15; i++) {
        out += snprintf(out, 3, "%02X", in[i] & 0xFF);
    }

    *out = '\0';
    return 0;
}

int AsusSMC::wmi_evaluate_method(uint32_t method_id, uint32_t arg0, uint32_t arg1) {
    OSObject *params[3];

    struct wmi_args args = {
        .arg0 = arg0,
        .arg1 = arg1
    };

    params[0] = OSNumber::withNumber(static_cast<uint32_t>(0), 32);
    params[1] = OSNumber::withNumber(method_id, 32);
    params[2] = OSData::withBytes(&args, sizeof(wmi_args));

    uint32_t val;
    IOReturn ret = atkDevice->evaluateInteger(wmi_method, &val, params, 3);
    params[0]->release();
    params[1]->release();
    params[2]->release();

    if (ret != kIOReturnSuccess) {
        DBGLOG("wmi", "wmi_evaluate_method failed");
        return -1;
    }

    if (val == 0xfffffffe) {
        DBGLOG("wmi", "wmi_evaluate_method invalid method_id");
        return -1;
    }

    return val;
}

int AsusSMC::wmi_get_devstate(uint32_t dev_id) {
    return wmi_evaluate_method(ASUS_WMI_METHODID_DSTS, dev_id, 0);
}

bool AsusSMC::wmi_dev_is_present(uint32_t dev_id) {
    int status = wmi_evaluate_method(ASUS_WMI_METHODID_DSTS, dev_id, 0);
    return status != -1 && (status & ASUS_WMI_DSTS_PRESENCE_BIT);
}

void AsusSMC::parse_WDG() {
    OSObject *wdg;
    if (atkDevice->evaluateObject("_WDG", &wdg) != kIOReturnSuccess) {
        SYSLOG("wmi", "No method _WDG!");
        return;
    }

    OSData *data = OSDynamicCast(OSData, wdg);
    if (!data) {
        SYSLOG("guid", "Cast WDG error!");
        return;
    }

    int total = data->getLength() / sizeof(struct guid_block);

    char guid_string[37];

    for (int i = 0; i < total; i++) {
        struct guid_block *g = (struct guid_block *) data->getBytesNoCopy(i * sizeof(struct guid_block), sizeof(struct guid_block));
        wmi_parse_guid(g->guid, guid_string);

        if (strncmp(guid_string, ASUS_WMI_MGMT_GUID, 36) == 0) {
            snprintf(wmi_method, 5, "WM%c%c", g->object_id[0], g->object_id[1]);
            DBGLOG("wmi", "parse_WDG found WMI method %s", wmi_method);
            return;
        }
    }

    // Couldn't find WMI method. Let's assume it's WMNB
    SYSLOG("wmi", "parse_WDG couldn't find WMI method");
    lilu_os_strncpy(wmi_method, "WMNB", 5);
}

void AsusSMC::initATKDevice() {
    wmi_evaluate_method(ASUS_WMI_METHODID_INIT, 0, 0);
}

void AsusSMC::initALSDevice() {
    auto dict = IOService::nameMatching("AppleACPIPlatformExpert");
    if (!dict) {
        SYSLOG("als", "WTF? Failed to create matching dictionary");
        return;
    }

    auto acpi = IOService::waitForMatchingService(dict);
    dict->release();

    if (!acpi) {
        SYSLOG("als", "WTF? No ACPI");
        return;
    }

    acpi->release();

    dict = IOService::nameMatching("ACPI0008");
    if (!dict) {
        SYSLOG("als", "WTF? Failed to create matching dictionary");
        return;
    }

    auto deviceIterator = IOService::getMatchingServices(dict);
    dict->release();

    if (!deviceIterator) {
        SYSLOG("als", "No iterator");
        return;
    }

    alsDevice = OSDynamicCast(IOACPIPlatformDevice, deviceIterator->getNextObject());
    deviceIterator->release();

    if (!alsDevice) {
        SYSLOG("als", "ACPI0008 device not found");
        return;
    }

    if (alsDevice->validateObject("_ALI") != kIOReturnSuccess || !refreshALS(false)) {
        SYSLOG("als", "No functional method _ALI on ALS device");
        return;
    }

    SYSLOG("als", "Found ALS Device %s", alsDevice->getName());
}

void AsusSMC::initEC0Device() {
    isTACHAvailable = true;

    auto dict = IOService::nameMatching("AppleACPIPlatformExpert");
    if (!dict) {
        SYSLOG("ec0", "WTF? Failed to create matching dictionary");
        isTACHAvailable = false;
        return;
    }

    auto acpi = IOService::waitForMatchingService(dict);
    dict->release();

    if (!acpi) {
        SYSLOG("ec0", "WTF? No ACPI");
        isTACHAvailable = false;
        return;
    }

    acpi->release();

    dict = IOService::nameMatching("PNP0C09");
    if (!dict) {
        SYSLOG("ec0", "WTF? Failed to create matching dictionary");
        isTACHAvailable = false;
        return;
    }

    auto deviceIterator = IOService::getMatchingServices(dict);
    dict->release();

    if (!deviceIterator) {
        SYSLOG("ec0", "No iterator");
        isTACHAvailable = false;
        return;
    }

    ec0Device = OSDynamicCast(IOACPIPlatformDevice, deviceIterator->getNextObject());
    deviceIterator->release();

    if (!ec0Device) {
        SYSLOG("ec0", "PNP0C09 device not found");
        isTACHAvailable = false;
        return;
    }

    if (ec0Device->validateObject("TACH") != kIOReturnSuccess || !refreshFan()) {
        SYSLOG("ec0", "No functional method TACH on EC0 device");
        isTACHAvailable = false;
        return;
    }

    SYSLOG("ec0", "Found EC0 Device %s", ec0Device->getName());
}

void AsusSMC::initBattery() {
    // Battery Health was introduced in 10.15.5
    // Check if we're on 10.15.5+
    if (getKernelVersion() < KernelVersion::Catalina || (getKernelVersion() == KernelVersion::Catalina && getKernelMinorVersion() < 5)) {
        return;
    }

    isBatteryRSOCAvailable = wmi_dev_is_present(ASUS_WMI_DEVID_RSOC);
    if (isBatteryRSOCAvailable) {
        toggleBatteryConservativeMode(true);
    }
}

void AsusSMC::initVirtualKeyboard() {
    kbdDevice = new VirtualAppleKeyboard;

    if (!kbdDevice || !kbdDevice->init() || !kbdDevice->attach(this) || !kbdDevice->start(this)) {
        OSSafeReleaseNULL(kbdDevice);
        SYSLOG("vkbd", "Failed to init VirtualAppleKeyboard");
    }
}

void AsusSMC::startATKDevice() {
    // Check direct ACPI messaging support
    if (atkDevice->validateObject("DMES") == kIOReturnSuccess) {
        DBGLOG("atk", "Direct ACPI message is supported");
        setProperty("IsDirectACPIMessagingSupported", kOSBooleanTrue);
        directACPImessaging = true;
    }

    // Check keyboard backlight support
    if (atkDevice->validateObject("SKBV") == kIOReturnSuccess) {
        SYSLOG("atk", "Keyboard backlight is supported");
        hasKeybrdBLight = true;
        addLKSBConsumer([](const uint16_t &value, OSObject *consumer) {
            auto atk = OSDynamicCast(IOACPIPlatformDevice, consumer);
            OSNumber *arg = OSNumber::withNumber(value / 16, 16);
            atk->evaluateObject("SKBV", NULL, (OSObject **)&arg, 1);
            arg->release();
        }, atkDevice);
    } else {
        hasKeybrdBLight = false;
        DBGLOG("atk", "Keyboard backlight is not supported");
    }
    setProperty("IsKeyboardBacklightSupported", hasKeybrdBLight);

    // Turn on ALS sensor
    toggleALS(true);
    isALSEnabled = true;
    setProperty("IsALSEnabled", isALSEnabled);
    SYSLOG("atk", "ALS is turned on at boot");
}

bool AsusSMC::refreshALS(bool post) {
    if (!alsDevice) {
        return false;
    }

    IOReturn ret = kIOReturnSuccess;
    uint32_t lux = 150;

    if (isALSEnabled) {
        ret = alsDevice->evaluateInteger("_ALI", &lux);
        if (ret != kIOReturnSuccess) {
            lux = 0xFFFFFFFF; // ACPI invalid
        }
    }

    atomic_store_explicit(&currentLux, lux, memory_order_release);

    if (post) {
        VirtualSMCAPI::postInterrupt(SmcEventALSChange);
        poller->setTimeoutMS(SensorUpdateTimeoutMS);
    }

    DBGLOG("als", "refreshALS lux %u", lux);

    return ret == kIOReturnSuccess;
}

bool AsusSMC::refreshFan() {
    uint32_t speed = 10000;

    if (isTACHAvailable) {
        OSNumber *arg = OSNumber::withNumber(static_cast<uint32_t>(0), 32);
        IOReturn ret = ec0Device->evaluateInteger("TACH", &speed, (OSObject **)&arg, 1);
        arg->release();

        if (ret != kIOReturnSuccess) {
            DBGLOG("fan", "read fan speed using TACH failed");
            speed = 10000;
        }
    } else {
        int ret = wmi_get_devstate(ASUS_WMI_DEVID_CPU_FAN_CTRL);
        if (ret == -1) {
            DBGLOG("fan", "read fan speed using WMI failed");
            speed = 10000;
        } else {
            speed = (ret & 0xffff) * 100;
        }
    }

    atomic_store_explicit(&currentFanSpeed, speed, memory_order_release);

    DBGLOG("fan", "refreshFan speed %u", speed);

    return speed != 10000;
}

void AsusSMC::handleMessage(int code) {
    switch (code) {
        case 0x57: // AC disconnected
        case 0x58: // AC connected
            // ignore silently
            break;

        case 0x30: // Volume up
            dispatchCSMRReport(kHIDUsage_Csmr_VolumeIncrement);
            break;

        case 0x31: // Volume down
            dispatchCSMRReport(kHIDUsage_Csmr_VolumeDecrement);
            break;

        case 0x32: // Mute
            dispatchCSMRReport(kHIDUsage_Csmr_Mute);
            break;

        // Media buttons
        case 0x40:
        case 0x8A:
            dispatchCSMRReport(kHIDUsage_Csmr_ScanPreviousTrack);
            break;

        case 0x41:
        case 0x82:
            dispatchCSMRReport(kHIDUsage_Csmr_ScanNextTrack);
            break;

        case 0x45:
        case 0x5C:
            dispatchCSMRReport(kHIDUsage_Csmr_PlayOrPause);
            break;

        case 0x33: // hardwired On
        case 0x34: // hardwired Off
        case 0x35: // Soft Event, Fn + F7
            displayOff();
            break;

        case 0x61: // Video Mirror
            dispatchTCReport(kHIDUsage_AV_TopCase_VideoMirror);
            break;

        case 0x6B: // Fn + F9, Touchpad On/Off
            toggleTouchpad();
            break;

        case 0x5E:
            letSleep();
            break;

        case 0x7A: // Fn + A, ALS Sensor
            // We should really do this in userspace
            // (e.g. "Automatically adjust brightness")
            isALSEnabled = !isALSEnabled;
            setProperty("IsALSEnabled", isALSEnabled);
            break;

        case 0x7D: // Airplane mode
            toggleAirplaneMode();
            break;

        case 0xC6:
        case 0xC7: // ALS Notifcations
            // ignore
            break;

        case 0xC5: // Keyboard Backlight Down
            if (hasKeybrdBLight) {
                dispatchTCReport(kHIDUsage_AV_TopCase_IlluminationDown);
            }
            break;

        case 0xC4: // Keyboard Backlight Up
            if (hasKeybrdBLight) {
                dispatchTCReport(kHIDUsage_AV_TopCase_IlluminationUp);
            }
            break;

        default:
            if (code >= NOTIFY_BRIGHTNESS_DOWN_MIN && code<= NOTIFY_BRIGHTNESS_DOWN_MAX) // Brightness Down
                dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessDown);
            else if (code >= NOTIFY_BRIGHTNESS_UP_MIN && code<= NOTIFY_BRIGHTNESS_UP_MAX) // Brightness Up
                dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessUp);
            break;
    }

    DBGLOG("atk", "Received key %d(0x%x)", code, code);
}

void AsusSMC::addLKSBConsumer(lksbCallback callback, OSObject *consumer) {
    auto *pcall = stored_pair<lksbCallback, OSObject *>::create();
    pcall->first = callback;
    pcall->second = consumer;

    IOLockLock(lksbLock);
    if (!LKSBCallbacks.push_back(pcall)) {
        SYSLOG("lksb", "failed to store lksb callback");
        stored_pair<lksbCallback, OSObject *>::deleter(pcall);
    }
    IOLockUnlock(lksbLock);
}

void AsusSMC::letSleep() {
    kev.sendMessage(kDaemonSleep, 0, 0);
}

void AsusSMC::toggleAirplaneMode() {
    kev.sendMessage(kDaemonAirplaneMode, 0, 0);
}

void AsusSMC::toggleTouchpad() {
    dispatchMessage(kKeyboardGetTouchStatus, &isTouchpadEnabled);
    isTouchpadEnabled = !isTouchpadEnabled;
    dispatchMessage(kKeyboardSetTouchStatus, &isTouchpadEnabled);

    if (isTouchpadEnabled) {
        setProperty("IsTouchpadEnabled", true);
        DBGLOG("atk", "Enabled Touchpad");
    } else {
        setProperty("IsTouchpadEnabled", false);
        DBGLOG("atk", "Disabled Touchpad");
    }
}

void AsusSMC::toggleALS(bool state) {
    if (wmi_evaluate_method(ASUS_WMI_METHODID_DEVS, ASUS_WMI_DEVID_ALS_ENABLE, state ? 1 : 0) == -1) {
        SYSLOG("atk", "Failed to %s ALSC", state ? "enable" : "disable");
    } else {
        DBGLOG("atk", "ALS is %s", state ? "enabled" : "disabled");
    }
}

void AsusSMC::toggleBatteryConservativeMode(bool state) {
    if (!isBatteryRSOCAvailable) {
        DBGLOG("batt", "RSOC unavailable");
        return;
    }

    if (wmi_evaluate_method(ASUS_WMI_METHODID_DEVS, ASUS_WMI_DEVID_RSOC, state ? 80 : 100) != 1) {
        SYSLOG("batt", "Failed to %s battery conservative mode", state ? "enable" : "disable");
    } else {
        DBGLOG("batt", "Battery conservative mode is %s", state ? "enabled" : "disabled");
        setProperty("BatteryConservativeMode", state);
    }
}

void AsusSMC::displayOff() {
    if (isPanelBackLightOn) {
        // Read Panel brigthness value to restore later with backlight toggle
        readPanelBrightnessValue();

        dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessDown, 16);
    } else {
        dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessUp, panelBrightnessLevel);
    }

    isPanelBackLightOn = !isPanelBackLightOn;
}

int AsusSMC::checkBacklightEntry() {
    if (IORegistryEntry *bkl = IORegistryEntry::fromPath(backlightEntry)) {
        OSSafeReleaseNULL(bkl);
        return 1;
    } else {
        DBGLOG("atk", "Failed to find backlight entry for %s", backlightEntry);
        return 0;
    }
}

int AsusSMC::findBacklightEntry() {
    // Check for previous found backlight entry
    if (checkBacklightEntry()) {
        return 1;
    }

    snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/IGPU@2/AppleIntelFramebuffer@0/display0/AppleBacklightDisplay");
    if (checkBacklightEntry()) {
        return 1;
    }

    snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/GFX0@2/AppleIntelFramebuffer@0/display0/AppleBacklightDisplay");
    if (checkBacklightEntry()) {
        return 1;
    }

    char deviceName[5][5] = {"PEG0", "PEGP", "PEGR", "P0P2", "IXVE"};
    for (int i = 0; i < 5; i++) {
        snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@1/IOPP/GFX0@0/NVDA,Display-A@0/NVDA/display0/AppleBacklightDisplay", deviceName[i]);
        if (checkBacklightEntry()) {
            return 1;
        }

        snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@3/IOPP/GFX0@0/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay", deviceName[i]);
        if (checkBacklightEntry()) {
            return 1;
        }

        snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@10/IOPP/GFX0@0/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay", deviceName[i]);
        if (checkBacklightEntry()) {
            return 1;
        }

        snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@1/IOPP/display@0/NVDA,Display-A@0/NVDA/display0/AppleBacklightDisplay", deviceName[i]);
        if (checkBacklightEntry()) {
            return 1;
        }

        snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@3/IOPP/display@0/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay", deviceName[i]);
        if (checkBacklightEntry()) {
            return 1;
        }

        snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@10/IOPP/display@0/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay", deviceName[i]);
        if (checkBacklightEntry()) {
            return 1;
        }
    }

    return 0;
}

void AsusSMC::readPanelBrightnessValue() {
    if (!findBacklightEntry()) {
        DBGLOG("atk", "GPU device not found");
        return;
    }

    IORegistryEntry *displayDeviceEntry = IORegistryEntry::fromPath(backlightEntry);

    if (displayDeviceEntry) {
        if (OSDictionary *ioDisplayParaDict = OSDynamicCast(OSDictionary, displayDeviceEntry->getProperty("IODisplayParameters"))) {
            if (OSDictionary *brightnessDict = OSDynamicCast(OSDictionary, ioDisplayParaDict->getObject("brightness"))) {
                if (OSNumber *brightnessValue = OSDynamicCast(OSNumber, brightnessDict->getObject("value"))) {
                    panelBrightnessLevel = brightnessValue->unsigned32BitValue() / 64;
                    DBGLOG("atk", "Panel brightness level: %d", panelBrightnessLevel);
                } else {
                    DBGLOG("atk", "Failed to read brightness value");
                }
            } else {
                DBGLOG("atk", "Failed to find dictionary brightness");
            }
        } else {
            DBGLOG("atk", "Failed to find dictionary IODisplayParameters");
        }
    }
    OSSafeReleaseNULL(displayDeviceEntry);
}

IOReturn AsusSMC::postKeyboardInputReport(const void *report, uint32_t reportSize) {
    IOReturn result = kIOReturnError;

    if (!report || reportSize == 0) {
        return kIOReturnBadArgument;
    }

    if (kbdDevice) {
        if (auto buffer = IOBufferMemoryDescriptor::withBytes(report, reportSize, kIODirectionNone)) {
            result = kbdDevice->handleReport(buffer, kIOHIDReportTypeInput, kIOHIDOptionsTypeNone);
            buffer->release();
        }
    }

    return result;
}

void AsusSMC::dispatchCSMRReport(int code, int loop) {
    DBGLOG("atk", "Dispatched key %d(0x%x), loop %d time(s)", code, code, loop);
    while (loop--) {
        csmrreport.keys.insert(code);
        postKeyboardInputReport(&csmrreport, sizeof(csmrreport));
        csmrreport.keys.erase(code);
        postKeyboardInputReport(&csmrreport, sizeof(csmrreport));
    }
}

void AsusSMC::dispatchTCReport(int code, int loop) {
    DBGLOG("atk", "Dispatched key %d(0x%x), loop %d time(s)", code, code, loop);
    while (loop--) {
        tcreport.keys.insert(code);
        postKeyboardInputReport(&tcreport, sizeof(tcreport));
        tcreport.keys.erase(code);
        postKeyboardInputReport(&tcreport, sizeof(tcreport));
    }
}

void AsusSMC::registerNotifications() {
    auto *key = OSSymbol::withCString(kDeliverNotifications);
    auto *propertyMatch = propertyMatching(key, kOSBooleanTrue);

    IOServiceMatchingNotificationHandler notificationHandler = OSMemberFunctionCast(IOServiceMatchingNotificationHandler, this, &AsusSMC::notificationHandler);

    _publishNotify = addMatchingNotification(gIOFirstPublishNotification,
                                             propertyMatch,
                                             notificationHandler,
                                             this,
                                             0, 10000);

    _terminateNotify = addMatchingNotification(gIOTerminatedNotification,
                                               propertyMatch,
                                               notificationHandler,
                                               this,
                                               0, 10000);

    key->release();
    propertyMatch->release();
}

void AsusSMC::notificationHandlerGated(IOService *newService, IONotifier *notifier) {
    if (notifier == _publishNotify) {
        SYSLOG("notify", "Notification consumer published: %s", newService->getName());
        _notificationServices->setObject(newService);
    }

    if (notifier == _terminateNotify) {
        SYSLOG("notify", "Notification consumer terminated: %s", newService->getName());
        _notificationServices->removeObject(newService);
    }
}

bool AsusSMC::notificationHandler(void *refCon, IOService *newService, IONotifier *notifier) {
    command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &AsusSMC::notificationHandlerGated), newService, notifier);
    return true;
}

void AsusSMC::dispatchMessageGated(int *message, void *data) {
    OSCollectionIterator *i = OSCollectionIterator::withCollection(_notificationServices);

    if (i != NULL) {
        while (IOService *service = OSDynamicCast(IOService, i->getNextObject())) {
            service->message(*message, this, data);
        }
        i->release();
    }
}

void AsusSMC::dispatchMessage(int message, void *data) {
    command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &AsusSMC::dispatchMessageGated), &message, data);
}

void AsusSMC::registerVSMC() {
    vsmcNotifier = VirtualSMCAPI::registerHandler(vsmcNotificationHandler, this);

    ALSSensor sensor {ALSSensor::Type::Unknown7, true, 6, false};
    ALSSensor noSensor {ALSSensor::Type::NoSensor, false, 0, false};
    SMCALSValue::Value emptyValue;
    SMCKBrdBLightValue::lkb lkb;
    SMCKBrdBLightValue::lks lks;

    VirtualSMCAPI::addKey(KeyAL, vsmcPlugin.data, VirtualSMCAPI::valueWithUint16(
        0, &forceBits,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE));

    VirtualSMCAPI::addKey(KeyALI0, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&sensor), sizeof(sensor), SmcKeyTypeAli, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyALI1, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&noSensor), sizeof(noSensor), SmcKeyTypeAli, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyALRV, vsmcPlugin.data, VirtualSMCAPI::valueWithUint16(
        1, nullptr,
        SMC_KEY_ATTRIBUTE_READ));

    VirtualSMCAPI::addKey(KeyALV0, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&emptyValue), sizeof(emptyValue), SmcKeyTypeAlv, new SMCALSValue(&currentLux, &forceBits),
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyALV1, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&emptyValue), sizeof(emptyValue), SmcKeyTypeAlv, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyLKSB, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&lkb), sizeof(lkb), SmcKeyTypeLkb, new SMCKBrdBLightValue(atkDevice, &LKSBCallbacks, lksbLock),
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyLKSS, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&lks), sizeof(lks), SmcKeyTypeLks, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyMSLD, vsmcPlugin.data, VirtualSMCAPI::valueWithUint8(
        0, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyFNum, vsmcPlugin.data, VirtualSMCAPI::valueWithUint8(
        1, nullptr,
        SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));

    VirtualSMCAPI::addKey(KeyF0Ac, vsmcPlugin.data, VirtualSMCAPI::valueWithFp(
        0, SmcKeyTypeFpe2, new F0Ac(&currentFanSpeed),
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_FUNCTION));

    FanTypeDescStruct desc;
    lilu_os_strncpy(desc.strFunction, "System Fan", DiagFunctionStrLen);

    VirtualSMCAPI::addKey(KeyF0ID, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&desc), sizeof(desc), SmcKeyTypeFds, nullptr,
        SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));

    if (isBatteryRSOCAvailable) {
        VirtualSMCAPI::addKey(KeyBDVT, vsmcPlugin.data, VirtualSMCAPI::valueWithFlag(
            false, new BDVT(this),
            SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_ATOMIC));
    }

    qsort(const_cast<VirtualSMCKeyValue *>(vsmcPlugin.data.data()), vsmcPlugin.data.size(), sizeof(VirtualSMCKeyValue), VirtualSMCKeyValue::compare);
}

bool AsusSMC::vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier) {
    if (sensors && vsmc) {
        DBGLOG("atk", "got vsmc notification");
        auto self = static_cast<AsusSMC *>(sensors);
        auto ret = vsmc->callPlatformFunction(VirtualSMCAPI::SubmitPlugin, true, sensors, &self->vsmcPlugin, nullptr, nullptr);
        if (ret == kIOReturnSuccess) {
            DBGLOG("atk", "Submitted plugin");

            self->workloop = self->getWorkLoop();
            self->poller = IOTimerEventSource::timerEventSource(self, [](OSObject *object, IOTimerEventSource *sender) {
                auto ls = OSDynamicCast(AsusSMC, object);
                if (ls) {
                    ls->refreshALS(true);
                    ls->refreshFan();
                }
            });

            if (!self->poller || !self->workloop) {
                SYSLOG("atk", "Failed to create poller or workloop");
                return false;
            }

            if (self->workloop->addEventSource(self->poller) != kIOReturnSuccess) {
                SYSLOG("atk", "Failed to add timer event source to workloop");
                return false;
            }

            if (self->poller->setTimeoutMS(SensorUpdateTimeoutMS) != kIOReturnSuccess) {
                SYSLOG("atk", "Failed to set timeout");
                return false;
            }

            return true;
        } else if (ret != kIOReturnUnsupported) {
            SYSLOG("atk", "Plugin submission failure %X", ret);
        } else {
            DBGLOG("atk", "Plugin submission to non vsmc");
        }
    } else {
        SYSLOG("atk", "Got null vsmc notification");
    }

    return false;
}

EXPORT extern "C" kern_return_t ADDPR(kern_start)(kmod_info_t *, void *) {
    // Report success but actually do not start and let I/O Kit unload us.
    // This works better and increases boot speed in some cases.
    PE_parse_boot_argn("liludelay", &ADDPR(debugPrintDelay), sizeof(ADDPR(debugPrintDelay)));
    ADDPR(debugEnabled) = checkKernelArgument("-vsmcdbg") || checkKernelArgument("-asussmcdbg");
    return KERN_SUCCESS;
}

EXPORT extern "C" kern_return_t ADDPR(kern_stop)(kmod_info_t *, void *) {
    // It is not safe to unload VirtualSMC plugins!
    return KERN_FAILURE;
}
