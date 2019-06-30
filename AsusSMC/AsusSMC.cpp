//
//  AsusSMC.cpp
//  AsusSMC
//
//  Copyright © 2018-2019 Le Bao Hiep. All rights reserved.
//

#include "AsusSMC.hpp"

bool ADDPR(debugEnabled) = true;
uint32_t ADDPR(debugPrintDelay) = 0;

#pragma mark -
#pragma mark WMI functions ported from Linux
#pragma mark -

int AsusSMC::wmi_data2Str(const char *in, char *out) {
    int i;

    for (i = 3; i >= 0; i--)
        out += snprintf(out, 3, "%02X", in[i] & 0xFF);

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

    for (i = 10; i <= 15; i++)
        out += snprintf(out, 3, "%02X", in[i] & 0xFF);

    *out = '\0';
    return 0;
}

OSString *AsusSMC::flagsToStr(UInt8 flags) {
    char str[80];
    char *pos = str;
    if (flags != 0) {
        if (flags & ACPI_WMI_EXPENSIVE) {
            lilu_os_strncpy(pos, "ACPI_WMI_EXPENSIVE ", 20);
            pos += strlen(pos);
        }
        if (flags & ACPI_WMI_METHOD) {
            lilu_os_strncpy(pos, "ACPI_WMI_METHOD ", 20);
            pos += strlen(pos);
            DBGLOG("guid", "WMI METHOD");
        }
        if (flags & ACPI_WMI_STRING) {
            lilu_os_strncpy(pos, "ACPI_WMI_STRING ", 20);
            pos += strlen(pos);
        }
        if (flags & ACPI_WMI_EVENT) {
            lilu_os_strncpy(pos, "ACPI_WMI_EVENT ", 20);
            pos += strlen(pos);
            DBGLOG("guid", "WMI EVENT");
        }
        //suppress the last trailing space
        str[strlen(str)] = 0;
    }
    else
        str[0] = 0;
    return (OSString::withCString(str));
}

void AsusSMC::wmi_wdg2reg(struct guid_block *g, OSArray *array, OSArray *dataArray) {
    char guid_string[37];
    char object_id_string[3];
    OSDictionary *dict = OSDictionary::withCapacity(6);

    wmi_data2Str(g->guid, guid_string);

    dict->setObject("UUID", OSString::withCString(guid_string));
    if (g->flags & ACPI_WMI_EVENT)
        dict->setObject("notify_value", OSNumber::withNumber(g->notify_id, 8));
    else {
        snprintf(object_id_string, 3, "%c%c", g->object_id[0], g->object_id[1]);
        dict->setObject("object_id", OSString::withCString(object_id_string));
    }
    dict->setObject("instance_count", OSNumber::withNumber(g->instance_count, 8));
    dict->setObject("flags", OSNumber::withNumber(g->flags, 8));
    dict->setObject("flags_str", flagsToStr(g->flags));
    if (g->flags == 0)
        dataArray->setObject(readDataBlock(object_id_string));

    array->setObject(dict);
}

OSDictionary *AsusSMC::readDataBlock(char *str) {
    OSDictionary *dict = OSDictionary::withCapacity(1);

    char name[5];
    snprintf(name, 5, "WQ%s", str);

    OSObject *wqxx;
    if (atkDevice->evaluateObject(name, &wqxx) != kIOReturnSuccess) {
        SYSLOG("guid", "No object of method %s", name);
        return dict;
    }

    OSData *data = OSDynamicCast(OSData, wqxx);
    if (!data) {
        SYSLOG("guid", "Cast error %s", name);
        return dict;
    }
    dict->setObject(name, data);

    return dict;
}

int AsusSMC::parse_wdg(OSDictionary *dict) {
    OSObject *wdg;
    if (atkDevice->evaluateObject("_WDG", &wdg) != kIOReturnSuccess) {
        SYSLOG("guid", "No object of method _WDG");
        return 0;
    }

    OSData *data = OSDynamicCast(OSData, wdg);
    if (!data) {
        SYSLOG("guid", "Cast error _WDG");
        return 0;
    }
    UInt32 total = data->getLength() / sizeof(struct guid_block);
    OSArray *array = OSArray::withCapacity(total);
    OSArray *dataArray = OSArray::withCapacity(1);

    for (UInt32 i = 0; i < total; i++)
        wmi_wdg2reg((struct guid_block *) data->getBytesNoCopy(i * sizeof(struct guid_block), sizeof(struct guid_block)), array, dataArray);
    setProperty("WDG", array);
    setProperty("DataBlocks", dataArray);
    data->release();

    return 0;
}

OSDictionary *AsusSMC::getDictByUUID(const char *guid) {
    OSArray *array = OSDynamicCast(OSArray, properties->getObject("WDG"));
    if (!array)
        return NULL;

    OSDictionary *dict = NULL;
    for (UInt32 i = 0; i < array->getCount(); i++) {
        dict = OSDynamicCast(OSDictionary, array->getObject(i));
        OSString *uuid = OSDynamicCast(OSString, dict->getObject("UUID"));
        if (uuid->isEqualTo(guid)) {
            break;
        }
    }
    return dict;
}

#pragma mark -
#pragma mark IOService overloading
#pragma mark -

#define super IOService

OSDefineMetaClassAndStructors(AsusSMC, IOService)

bool AsusSMC::init(OSDictionary *dict) {
    _notificationServices = OSSet::withCapacity(1);
    _hidDrivers = OSSet::withCapacity(1);

    kev.setVendorID("com.hieplpvip");
    kev.setEventCode(AsusSMCEventCode);

    atomic_init(&currentLux, 0);

    bool result = super::init(dict);
    properties = dict;

    DBGLOG("atk", "AsusSMC Inited");
    return result;
}

IOService *AsusSMC::probe(IOService *provider, SInt32 *score) {
    IOService *ret = NULL;

    if (!super::probe(provider, score))
        return ret;

    IOACPIPlatformDevice *dev = OSDynamicCast(IOACPIPlatformDevice, provider);
    if (!dev)
        return ret;

    OSObject *obj;
    dev->evaluateObject("_UID", &obj);

    OSString *name = OSDynamicCast(OSString, obj);
    if (!name)
        return ret;

    if (name->isEqualTo("ATK")) {
        *score += 20;
        ret = this;
    }
    name->release();

    return ret;
}

bool AsusSMC::start(IOService *provider) {
    DBGLOG("atk", "start is called");

    if (!provider || !super::start(provider)) {
        SYSLOG("atk", "Error loading kext");
        return false;
    }

    atkDevice = (IOACPIPlatformDevice *) provider;

    OSNumber *arg = OSNumber::withNumber(1, 8);
    atkDevice->evaluateObject("INIT", NULL, (OSObject**)&arg, 1);
    arg->release();

    SYSLOG("atk", "Found WMI Device %s", atkDevice->getName());

    parse_wdg(properties);

    checkATK();

    initVirtualKeyboard();

    registerNotifications();

    registerVSMC();

    this->registerService(0);

    workloop = getWorkLoop();
    if (!workloop) {
        DBGLOG("atk", "Failed to get workloop");
        return false;
    }
    workloop->retain();

    command_gate = IOCommandGate::commandGate(this);
    if (!command_gate)
        return false;

    workloop->addEventSource(command_gate);

    setProperty("AsusHIDHost", true);
    setProperty("IsTouchpadEnabled", true);
    setProperty("Copyright", "Copyright © 2018-2019 Le Bao Hiep. All rights reserved.");

    extern kmod_info_t kmod_info;
    setProperty("AsusSMC-Version", kmod_info.version);
#ifdef DEBUG
    setProperty("AsusSMC-Build", "Debug");
#else
    setProperty("AsusSMC-Build", "Release");
#endif
    return true;
}

void AsusSMC::stop(IOService *provider) {
    DBGLOG("atk", "stop is called");

    if (poller)
        poller->cancelTimeout();
    if (workloop && poller)
        workloop->removeEventSource(poller);
    if (workloop && command_gate)
        workloop->removeEventSource(command_gate);
    OSSafeReleaseNULL(workloop);
    OSSafeReleaseNULL(poller);
    OSSafeReleaseNULL(command_gate);

    _publishNotify->remove();
    _terminateNotify->remove();
    _notificationServices->flushCollection();
    OSSafeReleaseNULL(_publishNotify);
    OSSafeReleaseNULL(_terminateNotify);
    OSSafeReleaseNULL(_notificationServices);

    _hidDrivers->flushCollection();
    OSSafeReleaseNULL(_hidDrivers);

    OSSafeReleaseNULL(_virtualKBrd);

    super::stop(provider);
    return;
}

#pragma mark -
#pragma mark AsusSMC Methods
#pragma mark -

IOReturn AsusSMC::message(UInt32 type, IOService *provider, void *argument) {
    if (type == kIOACPIMessageDeviceNotification) {
        if (directACPImessaging) {
            handleMessage(*((UInt32 *) argument));
        } else {
            UInt32 event = *((UInt32 *) argument);
            OSNumber *arg = OSNumber::withNumber(event, sizeof(event) * 8);
            UInt32 res;
            atkDevice->evaluateInteger("_WED", &res, (OSObject**)&arg, 1);
            arg->release();

            handleMessage(res);
        }
    } else if (type == kAddAsusHIDDriver) {
        DBGLOG("atk", "Connected with HID driver");
        _hidDrivers->setObject(provider);
    } else if (type == kDelAsusHIDDriver) {
        DBGLOG("atk", "Disconnected with HID driver");
        _hidDrivers->removeObject(provider);
    } else {
        DBGLOG("atk", "Unexpected message: %u Type %x Provider %s", *((UInt32 *) argument), uint(type), provider->getName());
    }

    return kIOReturnSuccess;
}

void AsusSMC::handleMessage(int code) {
    // Processing the code
    switch (code) {
        case 0x57: // AC disconnected
        case 0x58: // AC connected
            // ignore silently
            break;

        case 0x30: // Volume up
            dispatchKBReport(kHIDUsage_Csmr_VolumeIncrement);
            break;

        case 0x31: // Volume down
            dispatchKBReport(kHIDUsage_Csmr_VolumeDecrement);
            break;

        case 0x32: // Mute
            dispatchKBReport(kHIDUsage_Csmr_Mute);
            break;

        // Media buttons
        case 0x40:
        case 0x8A:
            dispatchKBReport(kHIDUsage_Csmr_ScanPreviousTrack);
            break;

        case 0x41:
        case 0x82:
            dispatchKBReport(kHIDUsage_Csmr_ScanNextTrack);
            break;

        case 0x45:
        case 0x5C:
            dispatchKBReport(kHIDUsage_Csmr_PlayOrPause);
            break;

        case 0x33: // hardwired On
        case 0x34: // hardwired Off
        case 0x35: // Soft Event, Fn + F7
            if (isPanelBackLightOn) {
                // Read Panel brigthness value to restore later with backlight toggle
                readPanelBrightnessValue();

                dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessDown, 16);
            } else {
                dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessUp, panelBrightnessLevel);
            }

            isPanelBackLightOn = !isPanelBackLightOn;
            break;

        case 0x61: // Video Mirror
            dispatchTCReport(kHIDUsage_AV_TopCase_VideoMirror);
            break;

        case 0x6B: // Fn + F9, Touchpad On/Off
            touchpadEnabled = !touchpadEnabled;
            if (touchpadEnabled) {
                setProperty("IsTouchpadEnabled", true);
                DBGLOG("atk", "Enabled Touchpad");
            } else {
                setProperty("IsTouchpadEnabled", false);
                DBGLOG("atk", "Disabled Touchpad");
            }

            dispatchMessage(kKeyboardSetTouchStatus, &touchpadEnabled);
            break;

        case 0x5E:
            kev.sendMessage(kevSleep, 0, 0);
            break;

        case 0x7A: // Fn + A, ALS Sensor
            if (hasALSensor) {
                isALSenabled = !isALSenabled;
                toggleALS(isALSenabled);
            }
            break;

        case 0x7D: // Airplane mode
            kev.sendMessage(kevAirplaneMode, 0, 0);
            break;

        case 0xC6:
        case 0xC7: // ALS Notifcations
            // ignore
            break;

        case 0xC5: // Keyboard Backlight Down
            if (hasKeybrdBLight) dispatchTCReport(kHIDUsage_AV_TopCase_IlluminationDown);
            break;

        case 0xC4: // Keyboard Backlight Up
            if (hasKeybrdBLight) dispatchTCReport(kHIDUsage_AV_TopCase_IlluminationUp);
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

void AsusSMC::checkATK() {
    // Check direct ACPI messaging support
    if (atkDevice->validateObject("DMES") == kIOReturnSuccess) {
        DBGLOG("atk", "Direct ACPI message is supported");
        directACPImessaging = true;
    }

    // Check keyboard backlight support
    if (atkDevice->validateObject("SKBV") == kIOReturnSuccess) {
        SYSLOG("atk", "Keyboard backlight is supported");
        hasKeybrdBLight = true;
    }
    else {
        hasKeybrdBLight = false;
        DBGLOG("atk", "Keyboard backlight is not supported");
    }
    setProperty("IsKeyboardBacklightSupported", hasKeybrdBLight);

    // Check ALS sensor
    if (atkDevice->validateObject("ALSC") == kIOReturnSuccess && atkDevice->validateObject("ALSS") == kIOReturnSuccess) {
        SYSLOG("atk", "Found ALS sensor");
        hasALSensor = isALSenabled = true;
        toggleALS(isALSenabled);
        SYSLOG("atk", "ALS has been turned on at boot");
    } else {
        hasALSensor = false;
        DBGLOG("atk", "ALS sensor not found");
    }
    setProperty("IsALSSupported", hasALSensor);
}

void AsusSMC::toggleALS(bool state) {
    UInt32 res;
    OSNumber *arg = OSNumber::withNumber(state, sizeof(state) * 8);
    if (atkDevice->evaluateInteger("ALSC", &res, (OSObject**)&arg, 1) == kIOReturnSuccess)
        DBGLOG("atk", "ALS has been %s (ALSC ret %d)", state ? "enabled" : "disabled", res);
    else
        DBGLOG("atk", "Failed to call ALSC");
    setProperty("IsALSEnabled", state);
    arg->release();
}

int AsusSMC::checkBacklightEntry() {
    if (IORegistryEntry::fromPath(backlightEntry))
        return 1;
    else {
        DBGLOG("atk", "Failed to find backlight entry for %s", backlightEntry);
        return 0;
    }
}

int AsusSMC::findBacklightEntry() {
    snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/GFX0@2/AppleIntelFramebuffer@0/display0/AppleBacklightDisplay");
    if (checkBacklightEntry())
        return 1;

    snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/IGPU@2/AppleIntelFramebuffer@0/display0/AppleBacklightDisplay");
    if (checkBacklightEntry())
        return 1;

    char deviceName[5][5] = {"PEG0", "PEGP", "PEGR", "P0P2", "IXVE"};
    for (int i = 0; i < 5; i++) {
        snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@1/IOPP/GFX0@0", deviceName[i]);
        snprintf(backlightEntry, 1000, "%s%s", backlightEntry, "/NVDA,Display-A@0/NVDA/display0/AppleBacklightDisplay");
        if (checkBacklightEntry())
            return 1;

        snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@3/IOPP/GFX0@0", deviceName[i]);
        snprintf(backlightEntry, 1000, "%s%s", backlightEntry, "/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay");
        if (checkBacklightEntry())
            return 1;

        snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@10/IOPP/GFX0@0", deviceName[i]);
        snprintf(backlightEntry, 1000, "%s%s", backlightEntry, "/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay");
        if (checkBacklightEntry())
            return 1;

        snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@1/IOPP/display@0", deviceName[i]);
        snprintf(backlightEntry, 1000, "%s%s", backlightEntry, "/NVDA,Display-A@0/NVDA/display0/AppleBacklightDisplay");
        if (checkBacklightEntry())
            return 1;

        snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@3/IOPP/display@0", deviceName[i]);
        snprintf(backlightEntry, 1000, "%s%s", backlightEntry, "/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay");
        if (checkBacklightEntry())
            return 1;

        snprintf(backlightEntry, 1000, "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@10/IOPP/display@0", deviceName[i]);
        snprintf(backlightEntry, 1000, "%s%s", backlightEntry, "/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay");
        if (checkBacklightEntry())
            return 1;
    }

    return 0;
}

void AsusSMC::readPanelBrightnessValue() {
    if (!findBacklightEntry()) {
        DBGLOG("atk", "GPU device not found");
        return;
    }

    IORegistryEntry *displayDeviceEntry = IORegistryEntry::fromPath(backlightEntry);

    if (displayDeviceEntry != NULL) {
        if (OSDictionary *ioDisplayParaDict = OSDynamicCast(OSDictionary, displayDeviceEntry->getProperty("IODisplayParameters"))) {
            if (OSDictionary *brightnessDict = OSDynamicCast(OSDictionary, ioDisplayParaDict->getObject("brightness"))) {
                if (OSNumber *brightnessValue = OSDynamicCast(OSNumber, brightnessDict->getObject("value"))) {
                    panelBrightnessLevel = brightnessValue->unsigned32BitValue() / 64;
                    DBGLOG("atk", "Panel brightness level: %d", panelBrightnessLevel);
                } else
                    DBGLOG("atk", "Failed to read brightness value");
            } else
                DBGLOG("atk", "Failed to find dictionary brightness");
        } else
            DBGLOG("atk", "Failed to find dictionary IODisplayParameters");
    }
}

#pragma mark -
#pragma mark VirtualKeyboard
#pragma mark -

void AsusSMC::initVirtualKeyboard() {
    _virtualKBrd = new VirtualHIDKeyboard;

    if (!_virtualKBrd || !_virtualKBrd->init() || !_virtualKBrd->attach(this) || !_virtualKBrd->start(this)) {
        OSSafeReleaseNULL(_virtualKBrd);
        SYSLOG("virtkbrd", "Failed to init VirtualHIDKeyboard");
    } else
        _virtualKBrd->setCountryCode(0);
}

IOReturn AsusSMC::postKeyboardInputReport(const void *report, uint32_t reportSize) {
    IOReturn result = kIOReturnError;

    if (!report || reportSize == 0) {
        return kIOReturnBadArgument;
    }

    if (_virtualKBrd) {
        if (auto buffer = IOBufferMemoryDescriptor::withBytes(report, reportSize, kIODirectionNone)) {
            result = _virtualKBrd->handleReport(buffer, kIOHIDReportTypeInput, kIOHIDOptionsTypeNone);
            buffer->release();
        }
    }

    return result;
}

void AsusSMC::dispatchKBReport(int code, int loop)
{
    DBGLOG("atk", "Dispatched key %d(0x%x), loop %d time(s)", code, code, loop);
    while (loop--) {
        kbreport.keys.insert(code);
        postKeyboardInputReport(&kbreport, sizeof(kbreport));
        kbreport.keys.erase(code);
        postKeyboardInputReport(&kbreport, sizeof(kbreport));
    }
}

void AsusSMC::dispatchTCReport(int code, int loop)
{
    DBGLOG("atk", "Dispatched key %d(0x%x), loop %d time(s)", code, code, loop);
    while (loop--) {
        tcreport.keys.insert(code);
        postKeyboardInputReport(&tcreport, sizeof(tcreport));
        tcreport.keys.erase(code);
        postKeyboardInputReport(&tcreport, sizeof(tcreport));
    }
}

#pragma mark -
#pragma mark Notification methods
#pragma mark -

void AsusSMC::registerNotifications() {
    OSDictionary *propertyMatch = propertyMatching(OSSymbol::withCString(kDeliverNotifications), kOSBooleanTrue);

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
        while (IOService *service = OSDynamicCast(IOService, i->getNextObject()))
            service->message(*message, this, data);
        i->release();
    }
}

void AsusSMC::dispatchMessage(int message, void *data) {
    command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &AsusSMC::dispatchMessageGated), &message, data);
}

#pragma mark -
#pragma mark VirtualSMC plugin - Ported from SMCLightSensor
#pragma mark -

void AsusSMC::registerVSMC() {
    vsmcNotifier = VirtualSMCAPI::registerHandler(vsmcNotificationHandler, this);

    ALSSensor sensor {ALSSensor::Type::Unknown7, true, 6, false};
    ALSSensor noSensor {ALSSensor::Type::NoSensor, false, 0, false};
    SMCALSValue::Value emptyValue;
    SMCKBrdBLightValue::lkb lkb;
    SMCKBrdBLightValue::lks lks;

    VirtualSMCAPI::addKey(KeyAL, vsmcPlugin.data, VirtualSMCAPI::valueWithUint16(0, &forceBits, SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE));

    VirtualSMCAPI::addKey(KeyALI0, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&sensor), sizeof(sensor), SmcKeyTypeAli, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyALI1, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&noSensor), sizeof(noSensor), SmcKeyTypeAli, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyALRV, vsmcPlugin.data, VirtualSMCAPI::valueWithUint16(1, nullptr, SMC_KEY_ATTRIBUTE_READ));

    VirtualSMCAPI::addKey(KeyALV0, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&emptyValue), sizeof(emptyValue), SmcKeyTypeAlv, new SMCALSValue(&currentLux, &forceBits),
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyALV1, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&emptyValue), sizeof(emptyValue), SmcKeyTypeAlv, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyLKSB, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&lkb), sizeof(lkb), SmcKeyTypeLkb, new SMCKBrdBLightValue(atkDevice, _hidDrivers),
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyLKSS, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&lks), sizeof(lks), SmcKeyTypeLks, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyMSLD, vsmcPlugin.data, VirtualSMCAPI::valueWithUint8(0, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));
}

bool AsusSMC::vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier) {
    if (sensors && vsmc) {
        DBGLOG("alsd", "got vsmc notification");
        auto self = static_cast<AsusSMC *>(sensors);
        auto ret = vsmc->callPlatformFunction(VirtualSMCAPI::SubmitPlugin, true, sensors, &self->vsmcPlugin, nullptr, nullptr);
        if (ret == kIOReturnSuccess) {
            DBGLOG("alsd", "Submitted plugin");

            self->workloop = self->getWorkLoop();
            self->poller = IOTimerEventSource::timerEventSource(self, [](OSObject *object, IOTimerEventSource *sender) {
                auto ls = OSDynamicCast(AsusSMC, object);
                if (ls) ls->refreshSensor(true);
            });

            if (!self->poller || !self->workloop) {
                SYSLOG("alsd", "Failed to create poller or workloop");
                return false;
            }

            if (self->workloop->addEventSource(self->poller) != kIOReturnSuccess) {
                SYSLOG("alsd", "Failed to add timer event source to workloop");
                return false;
            }

            if (self->poller->setTimeoutMS(SensorUpdateTimeoutMS) != kIOReturnSuccess) {
                SYSLOG("alsd", "Failed to set timeout");
                return false;
            }

            return true;
        } else if (ret != kIOReturnUnsupported) {
            SYSLOG("alsd", "Plugin submission failure %X", ret);
        } else {
            DBGLOG("alsd", "Plugin submission to non vsmc");
        }
    } else {
        SYSLOG("alsd", "Got null vsmc notification");
    }

    return false;
}

bool AsusSMC::refreshSensor(bool post) {
    uint32_t lux = 0;
    auto ret = atkDevice->evaluateInteger("ALSS", &lux);
    if (ret != kIOReturnSuccess)
        lux = 0xFFFFFFFF; // ACPI invalid

    atomic_store_explicit(&currentLux, lux, memory_order_release);

    if (post) {
        VirtualSMCAPI::postInterrupt(SmcEventALSChange);
        poller->setTimeoutMS(SensorUpdateTimeoutMS);
    }

    DBGLOG("alsd", "refreshSensor lux %u", lux);

    return ret == kIOReturnSuccess;
}

EXPORT extern "C" kern_return_t ADDPR(kern_start)(kmod_info_t *, void *) {
    // Report success but actually do not start and let I/O Kit unload us.
    // This works better and increases boot speed in some cases.
    PE_parse_boot_argn("liludelay", &ADDPR(debugPrintDelay), sizeof(ADDPR(debugPrintDelay)));
    ADDPR(debugEnabled) = checkKernelArgument("-asussmcdbg");
    return KERN_SUCCESS;
}

EXPORT extern "C" kern_return_t ADDPR(kern_stop)(kmod_info_t *, void *) {
    // It is not safe to unload VirtualSMC plugins!
    return KERN_FAILURE;
}
