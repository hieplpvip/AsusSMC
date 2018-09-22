//
//  AsusSMC.cpp
//  AsusSMC
//
//  Copyright © 2018 Le Bao Hiep. All rights reserved.
//

#include "AsusSMC.hpp"

bool ADDPR(debugEnabled) = true;
uint32_t ADDPR(debugPrintDelay) = 0;

#pragma mark -
#pragma mark WMI code ported from Linux
#pragma mark -

/*
 * GUID parsing functions
 */
int AsusSMC::wmi_parse_hexbyte(const UInt8 *src) {
    unsigned int x; /* For correct wrapping */
    int h;

    /* high part */
    x = src[0];
    if (x - '0' <= '9' - '0') {
        h = x - '0';
    } else if (x - 'a' <= 'f' - 'a') {
        h = x - 'a' + 10;
    } else if (x - 'A' <= 'F' - 'A') {
        h = x - 'A' + 10;
    } else {
        return -1;
    }
    h <<= 4;

    /* low part */
    x = src[1];
    if (x - '0' <= '9' - '0')
        return h | (x - '0');
    if (x - 'a' <= 'f' - 'a')
        return h | (x - 'a' + 10);
    if (x - 'A' <= 'F' - 'A')
        return h | (x - 'A' + 10);
    return -1;
}

void AsusSMC::wmi_swap_bytes(UInt8 *src, UInt8 *dest) {
    int i;

    for (i = 0; i <= 3; i++)
        lilu_os_memcpy(dest + i, src + (3 - i), 1);

    for (i = 0; i <= 1; i++)
        lilu_os_memcpy(dest + 4 + i, src + (5 - i), 1);

    for (i = 0; i <= 1; i++)
        lilu_os_memcpy(dest + 6 + i, src + (7 - i), 1);

    lilu_os_memcpy(dest + 8, src + 8, 8);
}

bool AsusSMC::wmi_parse_guid(const UInt8 *src, UInt8 *dest) {
    static const int size[] = { 4, 2, 2, 2, 6 };
    int i, j, v;

    if (src[8]  != '-' || src[13] != '-' ||
        src[18] != '-' || src[23] != '-')
        return false;

    for (j = 0; j < 5; j++, src++) {
        for (i = 0; i < size[j]; i++, src += 2, *dest++ = v) {
            v = wmi_parse_hexbyte(src);
            if (v < 0)
                return false;
        }
    }

    return true;
}

void AsusSMC::wmi_dump_wdg(struct guid_block *src) {
    char guid_string[37];

    wmi_data2Str(src->guid, guid_string);
    DBGLOG("atk", "%s:\n", guid_string);
    if (src->flags & ACPI_WMI_EVENT)
        DBGLOG("atk", "\tnotify_value: %02X\n", src->notify_id);
    else
        DBGLOG("atk", "\tobject_id: %c%c\n",src->object_id[0], src->object_id[1]);
    DBGLOG("atk", "\tinstance_count: %d\n", src->instance_count);
    DBGLOG("atk", "\tflags: %#x", src->flags);
    if (src->flags) {
        DBGLOG("atk", " ");
        if (src->flags & ACPI_WMI_EXPENSIVE)
            DBGLOG("atk", "ACPI_WMI_EXPENSIVE ");
        if (src->flags & ACPI_WMI_METHOD)
            DBGLOG("atk", "ACPI_WMI_METHOD ");
        if (src->flags & ACPI_WMI_STRING)
            DBGLOG("atk", "ACPI_WMI_STRING ");
        if (src->flags & ACPI_WMI_EVENT)
            DBGLOG("atk", "ACPI_WMI_EVENT ");
    }
}

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

OSString * AsusSMC::flagsToStr(UInt8 flags) {
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
            DBGLOG("atk", "WMI METHOD\n");
        }
        if (flags & ACPI_WMI_STRING) {
            lilu_os_strncpy(pos, "ACPI_WMI_STRING ", 20);
            pos += strlen(pos);
        }
        if (flags & ACPI_WMI_EVENT) {
            lilu_os_strncpy(pos, "ACPI_WMI_EVENT ", 20);
            pos += strlen(pos);
            DBGLOG("atk", "WMI EVENT\n");
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
#if DEBUG
    dict->setObject("flags Str", flagsToStr(g->flags));
#endif
    if (g->flags == 0)
        dataArray->setObject(readDataBlock(object_id_string));

    array->setObject(dict);
}

OSDictionary * AsusSMC::readDataBlock(char *str) {
    OSObject    *wqxx;
    OSData        *data = NULL;
    OSDictionary *dict;
    char name[5];

    snprintf(name, 5, "WQ%s", str);
    dict = OSDictionary::withCapacity(1);

    do {
        if (atkDevice->evaluateObject(name, &wqxx) != kIOReturnSuccess) {
            IOLog("%s::No object of method %s\n", getName(), name);
            continue;
        }

        data = OSDynamicCast(OSData, wqxx);
        if (data == NULL) {
            IOLog("%s::Cast error %s\n", getName(), name);
            continue;
        }
        dict->setObject(name, data);
    } while (false);
    return dict;
}

int AsusSMC::parse_wdg(OSDictionary *dict) {
    UInt32 i, total;
    OSObject *wdg;
    OSData *data;
    OSArray *array, *dataArray;

    do {
        if (atkDevice->evaluateObject("_WDG", &wdg) != kIOReturnSuccess) {
            IOLog("%s::No object of method _WDG\n", getName());
            continue;
        }

        data = OSDynamicCast(OSData, wdg);
        if (data == NULL) {
            IOLog("%s::Cast error _WDG\n", getName());
            continue;
        }
        total = data->getLength() / sizeof(struct guid_block);
        array = OSArray::withCapacity(total);
        dataArray = OSArray::withCapacity(1);

        for (i = 0; i < total; i++)
            wmi_wdg2reg((struct guid_block *) data->getBytesNoCopy(i * sizeof(struct guid_block), sizeof(struct guid_block)), array, dataArray);
        setProperty("WDG", array);
        setProperty("DataBlocks", dataArray);
        data->release();
    } while (false);

    return 0;
}

#pragma mark -
#pragma mark IOService overloading
#pragma mark -

#define super IOService

OSDefineMetaClassAndStructors(AsusSMC, IOService)

const FnKeysKeyMap AsusSMC::keyMap[] = {
    {0x30, kHIDUsage_Csmr_VolumeIncrement, ReportType::keyboard_input},
    {0x31, kHIDUsage_Csmr_VolumeDecrement, ReportType::keyboard_input},
    {0x32, kHIDUsage_Csmr_Mute, ReportType::keyboard_input},
    {0x61, kHIDUsage_AV_TopCase_VideoMirror, ReportType::apple_vendor_top_case_input},
    {0x10, kHIDUsage_AV_TopCase_BrightnessUp, ReportType::apple_vendor_top_case_input},
    {0x20, kHIDUsage_AV_TopCase_BrightnessDown, ReportType::apple_vendor_top_case_input},
    // Keyboard backlight
    {0xC5, kHIDUsage_AV_TopCase_IlluminationDown, ReportType::apple_vendor_top_case_input},
    {0xC4, kHIDUsage_AV_TopCase_IlluminationUp, ReportType::apple_vendor_top_case_input},
    // Media buttons bound to Asus events keys Down, Left and Right Arrows in full keyboard
    {0x40, kHIDUsage_Csmr_ScanPreviousTrack, ReportType::keyboard_input},
    {0x41, kHIDUsage_Csmr_ScanNextTrack, ReportType::keyboard_input},
    {0x45, kHIDUsage_Csmr_PlayOrPause, ReportType::keyboard_input},
    // Media button bound to Asus events keys C, V and Space keys in compact keyboard
    {0x8A, kHIDUsage_Csmr_ScanPreviousTrack, ReportType::keyboard_input},
    {0x82, kHIDUsage_Csmr_ScanNextTrack, ReportType::keyboard_input},
    {0x5C, kHIDUsage_Csmr_PlayOrPause, ReportType::keyboard_input},
    {0,0xFF,ReportType::none}
};

bool AsusSMC::init(OSDictionary *dict) {
    _notificationServices = OSSet::withCapacity(1);

    kev.setVendorID("com.hieplpvip");
    kev.setEventCode(AsusSMCEventCode);

    bool result = super::init(dict);
    properties = dict;
    DBGLOG("atk", "Init\n");
    return result;
}

IOService * AsusSMC::probe(IOService *provider, SInt32 *score) {
    IOService * ret = NULL;
    OSObject * obj;
    OSString * name;
    IOACPIPlatformDevice *dev;
    do {
        if (!super::probe(provider, score))
            continue;

        dev = OSDynamicCast(IOACPIPlatformDevice, provider);
        if (NULL == dev)
            continue;

        dev->evaluateObject("_UID", &obj);

        name = OSDynamicCast(OSString, obj);
        if (NULL == name)
            continue;

        if (name->isEqualTo("ATK")) {
            *score +=20;
            ret = this;
        }
        name->release();
    } while (false);

    return (ret);
}

bool AsusSMC::start(IOService *provider) {
    if (!provider || !super::start(provider)) {
        IOLog("%s::Error loading kext\n", getName());
        return false;
    }

    atkDevice = (IOACPIPlatformDevice *) provider;
    atkDevice->evaluateObject("INIT", NULL, NULL, NULL);

    IOLog("%s::Found WMI Device %s\n", getName(), atkDevice->getName());

    parse_wdg(properties);

    checkKBALS();

    initVirtualKeyboard();

    registerNotifications();

    registerVSMC();

    PMinit();
    registerPowerDriver(this, powerStateArray, kAsusSMCIOPMNumberPowerStates);
    provider->joinPMtree(this);

    this->registerService(0);

    workloop = getWorkLoop();
    if (!workloop) {
        DBGLOG("atk", "Failed to get workloop!\n");
        return false;
    }
    workloop->retain();

    command_gate = IOCommandGate::commandGate(this);
    if (!command_gate)
        return false;

    workloop->addEventSource(command_gate);

    setProperty("TouchpadEnabled", true);

    setProperty("Copyright", "Copyright © 2018 hieplpvip. Credits: EMlyDinEsH(OSXLatitude)");

    IOLog("%s::Asus Fn Hotkey Events Enabled\n", getName());

    return true;
}

void AsusSMC::stop(IOService *provider) {
    DBGLOG("atk", "Stop\n");

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

    OSSafeReleaseNULL(_virtualKBrd);
    PMstop();

    super::stop(provider);
    return;
}

IOReturn AsusSMC::setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) {
    if (whatDevice != this)
        return IOPMAckImplied;

    if (!powerStateOrdinal)
        DBGLOG("atk", "Going to sleep\n");
    else {
        DBGLOG("atk", "Woke up from sleep\n");
        IOSleep(1000);
    }

    return IOPMAckImplied;
}

#pragma mark -
#pragma mark AsusSMC Methods
#pragma mark -

IOReturn AsusSMC::message(UInt32 type, IOService * provider, void * argument) {
    if (type == kIOACPIMessageDeviceNotification) {
        UInt32 event = *((UInt32 *) argument);
        OSObject * wed;

        OSNumber * number = OSNumber::withNumber(event,32);
        atkDevice->evaluateObject("_WED", &wed, (OSObject**)&number,1);
        number->release();
        number = OSDynamicCast(OSNumber, wed);
        if (NULL == number) {
            // try a package
            OSArray * array = OSDynamicCast(OSArray, wed);
            if (NULL == array) {
                // try a buffer
                OSData * data = OSDynamicCast(OSData, wed);
                if ((NULL == data) || (data->getLength() == 0)) {
                    DBGLOG("atk", "Fail to cast _WED returned objet %s\n", wed->getMetaClass()->getClassName());
                    return kIOReturnError;
                }
                const char * bytes = (const char *) data->getBytesNoCopy();
                number = OSNumber::withNumber(bytes[0],32);
            } else {
                number = OSDynamicCast(OSNumber, array->getObject(0));
                if (NULL == number) {
                    DBGLOG("atk", "Fail to cast _WED returned 1st objet in array %s\n", array->getObject(0)->getMetaClass()->getClassName());
                    return kIOReturnError;
                }
            }
        }

        handleMessage(number->unsigned32BitValue());
    }
    else
        DBGLOG("atk", "Unexpected message: %u Type %x Provider %s \n", *((UInt32 *) argument), uint(type), provider->getName());

    return kIOReturnSuccess;
}

void AsusSMC::handleMessage(int code) {
    int loopCount = 0;

    // Processing the code
    switch (code) {
        case 0x57: // AC disconnected
        case 0x58: // AC connected
            // ignore silently
            break;

        // Backlight
        case 0x33:// hardwired On
        case 0x34:// hardwired Off
        case 0x35:// Soft Event, Fn + F7
            if (isPanelBackLightOn) {
                code = NOTIFY_BRIGHTNESS_DOWN_MIN;
                loopCount = 16;

                // Read Panel brigthness value to restore later with backlight toggle
                readPanelBrightnessValue();
            } else {
                code = NOTIFY_BRIGHTNESS_UP_MIN;
                loopCount = panelBrightnessLevel;
            }

            isPanelBackLightOn = !isPanelBackLightOn;
            break;

        case 0x6B: // Fn + F9, Touchpad On/Off
            touchpadEnabled = !touchpadEnabled;
            if (touchpadEnabled) {
                setProperty("TouchpadEnabled", true);
                removeProperty("TouchpadDisabled");
                DBGLOG("atk", "Touchpad Enabled\n");
            } else {
                removeProperty("TouchpadEnabled");
                setProperty("TouchpadDisabled", true);
                DBGLOG("atk", "Touchpad Disabled\n");
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
            if (hasALSensor) {
                UInt32 alsValue = 0;
                atkDevice->evaluateInteger("ALSS", &alsValue, NULL, NULL);
                DBGLOG("atk", "ALS %d\n", alsValue);
            }
            break;

        case 0xC5: // Fn + F3, Decrease Keyboard Backlight
        case 0xC4: // Fn + F4, Increase Keyboard Backlight
            if (!hasKeybrdBLight) code = 0;
            break;

        default:
            if (code >= NOTIFY_BRIGHTNESS_DOWN_MIN && code<= NOTIFY_BRIGHTNESS_DOWN_MAX) {
                // Fn + F5, Panel Brightness Down
                code = NOTIFY_BRIGHTNESS_DOWN_MIN;

                if (panelBrightnessLevel > 0)
                    panelBrightnessLevel--;
            } else if (code >= NOTIFY_BRIGHTNESS_UP_MIN && code<= NOTIFY_BRIGHTNESS_UP_MAX) {
                // Fn + F6, Panel Brightness Up
                code = NOTIFY_BRIGHTNESS_UP_MIN;

                panelBrightnessLevel++;
                if (panelBrightnessLevel>16)
                    panelBrightnessLevel = 16;
            }
            break;
    }

    DBGLOG("atk", "Received Key %d(0x%x)\n", code, code);

    // Sending the code for the keyboard handler
    processFnKeyEvents(code, loopCount);
}

void AsusSMC::processFnKeyEvents(int code, int bLoopCount) {
    // TO-DO: rewrite this
    int i = 0, out;
    ReportType type;
    do {
        if (keyMap[i].type == ReportType::none && keyMap[i].in == 0 && keyMap[i].out == 0xFF) {
            DBGLOG("atk", "Unknown key %02X i=%d\n", code, i);
            return;
        }
        if (keyMap[i].in == code) {
            DBGLOG("atk", "Key Pressed %02X i=%d\n", code, i);
            out = keyMap[i].out;
            type = keyMap[i].type;
            break;
        }
        i++;
    } while (true);

    if (type == ReportType::keyboard_input) {
        if (bLoopCount > 0) {
            while (bLoopCount--) {
                kbreport.keys.insert(out);
                postKeyboardInputReport(&kbreport, sizeof(kbreport));
                kbreport.keys.erase(out);
                postKeyboardInputReport(&kbreport, sizeof(kbreport));
            }
            DBGLOG("atk", "Loop Count %d, Dispatch Key %d(0x%x)\n", bLoopCount, code, code);
        } else {
            kbreport.keys.insert(out);
            postKeyboardInputReport(&kbreport, sizeof(kbreport));
            kbreport.keys.erase(out);
            postKeyboardInputReport(&kbreport, sizeof(kbreport));
            DBGLOG("atk", "Dispatch Key %d(0x%x)\n", code, code);
        }
    }

    else if (type == ReportType::apple_vendor_top_case_input) {
        if (bLoopCount > 0) {
            while (bLoopCount--) {
                tcreport.keys.insert(out);
                postKeyboardInputReport(&tcreport, sizeof(tcreport));
                tcreport.keys.erase(out);
                postKeyboardInputReport(&tcreport, sizeof(tcreport));
            }
            DBGLOG("atk", "Loop Count %d, Dispatch Key %d(0x%x)\n", bLoopCount, code, code);
        } else {
            tcreport.keys.insert(out);
            postKeyboardInputReport(&tcreport, sizeof(tcreport));
            tcreport.keys.erase(out);
            postKeyboardInputReport(&tcreport, sizeof(tcreport));
            DBGLOG("atk", "Dispatch Key %d(0x%x)\n", code, code);
        }
    }
}

void AsusSMC::checkKBALS() {
    // Check keyboard backlight support
    if (atkDevice->validateObject("SKBV") == kIOReturnSuccess)
        hasKeybrdBLight = true;
    else {
        hasKeybrdBLight = false;
        DBGLOG("atk", "Keyboard backlight is not supported\n");
    }

    // Check ALS sensor
    if (atkDevice->validateObject("ALSC") == kIOReturnSuccess && atkDevice->validateObject("ALSS") == kIOReturnSuccess) {
        IOLog("%s::Found ALS sensor\n", getName());
        hasALSensor = isALSenabled = true;
        toggleALS(isALSenabled);
        IOLog("%s::ALS turned on at boot\n", getName());
    } else {
        hasALSensor = false;
        DBGLOG("atk", "No ALS sensors were found\n");
    }
}

void AsusSMC::toggleALS(bool state) {
    OSObject * params[1];
    UInt32 res;
    params[0] = OSNumber::withNumber(state, 8);

    if (atkDevice->evaluateInteger("ALSC", &res, params, 1) == kIOReturnSuccess)
        DBGLOG("atk", "ALS %s %d\n", state ? "enabled" : "disabled", res);
    else
        DBGLOG("atk", "Failed to call ALSC\n");
}

int AsusSMC::checkBacklightEntry() {
    if (IORegistryEntry::fromPath(backlightEntry))
        return 1;
    else {
        DBGLOG("atk", "Failed to find backlight entry for %s\n", backlightEntry);
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
        DBGLOG("atk", "GPU device not found\n");
        return;
    }

    IORegistryEntry *displayDeviceEntry = IORegistryEntry::fromPath(backlightEntry);

    if (displayDeviceEntry != NULL) {
        if (OSDictionary* ioDisplayParaDict = OSDynamicCast(OSDictionary, displayDeviceEntry->getProperty("IODisplayParameters"))) {
            if (OSDictionary* brightnessDict = OSDynamicCast(OSDictionary, ioDisplayParaDict->getObject("brightness"))) {
                if (OSNumber* brightnessValue = OSDynamicCast(OSNumber, brightnessDict->getObject("value"))) {
                    panelBrightnessLevel = brightnessValue->unsigned32BitValue()/64;
                    DBGLOG("atk", "Panel brightness level from AppleBacklightDisplay: %d\n", brightnessValue->unsigned32BitValue());
                    DBGLOG("atk", "Read panel brightness level: %d\n", panelBrightnessLevel);
                } else
                    DBGLOG("atk", "Can't not read brightness value\n");
            } else
                DBGLOG("atk", "Can't not find dictionary brightness\n");
        } else
            DBGLOG("atk", "Can't not find dictionary IODisplayParameters\n");
    }
}

void AsusSMC::getDeviceStatus(const char * guid, UInt32 methodId, UInt32 deviceId, UInt32 *status) {
    DBGLOG("atk", "getDeviceStatus()\n");

    char method[5];
    OSObject * params[3];
    OSString *str;
    OSDictionary *dict = getDictByUUID(guid);
    if (NULL == dict)
        return;

    str = OSDynamicCast(OSString, dict->getObject("object_id"));
    if (NULL == str)
        return;

    snprintf(method, 5, "WM%s", str->getCStringNoCopy());

    params[0] = OSNumber::withNumber(0x00D,32);
    params[1] = OSNumber::withNumber(methodId,32);
    params[2] = OSNumber::withNumber(deviceId,32);

    atkDevice->evaluateInteger(method, status, params, 3);

    params[0]->release();
    params[1]->release();
    params[2]->release();
}

void AsusSMC::setDeviceStatus(const char * guid, UInt32 methodId, UInt32 deviceId, UInt32 *status) {
    DBGLOG("atk", "setDeviceStatus()\n");

    char method[5];
    char buffer[8];
    OSObject * params[3];
    OSString *str;
    OSDictionary *dict = getDictByUUID(guid);
    if (NULL == dict)
        return;

    str = OSDynamicCast(OSString, dict->getObject("object_id"));
    if (NULL == str)
        return;

    snprintf(method, 5, "WM%s", str->getCStringNoCopy());

    lilu_os_memcpy(buffer, &deviceId, 4);
    lilu_os_memcpy(buffer+4, status, 4);

    params[0] = OSNumber::withNumber(0x00D,32);
    params[1] = OSNumber::withNumber(methodId,32);
    params[2] = OSData::withBytes(buffer, 8);

    *status = ~0;
    atkDevice->evaluateInteger(method, status, params, 3);

    DBGLOG("atk", "setDeviceStatus Res = %x\n", (unsigned int)*status);

    params[0]->release();
    params[1]->release();
    params[2]->release();
}

void AsusSMC::setDevice(const char * guid, UInt32 methodId, UInt32 *status) {
    DBGLOG("atk", "setDevice(%d)\n", (int)*status);

    char method[5];
    char buffer[4];
    OSObject * params[3];
    OSString *str;
    OSDictionary *dict = getDictByUUID(guid);
    if (NULL == dict)
        return;

    str = OSDynamicCast(OSString, dict->getObject("object_id"));
    if (NULL == str)
        return;

    snprintf(method, 5, "WM%s", str->getCStringNoCopy());

    lilu_os_memcpy(buffer, status, 4);

    params[0] = OSNumber::withNumber(0x00D,32);
    params[1] = OSNumber::withNumber(methodId,32);
    params[2] = OSData::withBytes(buffer, 8);

    *status = ~0;
    atkDevice->evaluateInteger(method, status, params, 3);

    DBGLOG("atk", "setDevice Res = %x\n", (unsigned int)*status);

    params[0]->release();
    params[1]->release();
    params[2]->release();

    return;
}

OSDictionary* AsusSMC::getDictByUUID(const char * guid) {
    UInt32 i;
    OSDictionary *dict = NULL;
    OSString *uuid;
    OSArray *array = OSDynamicCast(OSArray, properties->getObject("WDG"));
    if (NULL == array)
        return NULL;
    for (i = 0; i < array->getCount(); i++) {
        dict = OSDynamicCast(OSDictionary, array->getObject(i));
        uuid = OSDynamicCast(OSString, dict->getObject("UUID"));
        if (uuid->isEqualTo(guid)) {
            break;
        }
    }
    return dict;
}

#pragma mark -
#pragma mark VirtualKeyboard
#pragma mark -

void AsusSMC::initVirtualKeyboard() {
    _virtualKBrd = new VirtualHIDKeyboard;

    if (!_virtualKBrd || !_virtualKBrd->init() || !_virtualKBrd->attach(this) || !_virtualKBrd->start(this)) {
        _virtualKBrd->release();
        IOLog("%s::Error VirtualHIDKeyboard\n", getName());
    } else
        _virtualKBrd->setCountryCode(0);
}

IOReturn AsusSMC::postKeyboardInputReport(const void* report, uint32_t reportSize) {
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

#pragma mark -
#pragma mark Notification methods
#pragma mark -

void AsusSMC::registerNotifications() {
    OSDictionary * propertyMatch = propertyMatching(OSSymbol::withCString(kDeliverNotifications), OSBoolean::withBoolean(true));

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

void AsusSMC::notificationHandlerGated(IOService * newService, IONotifier * notifier) {
    if (notifier == _publishNotify) {
        IOLog("%s::Notification consumer published: %s\n", getName(), newService->getName());
        _notificationServices->setObject(newService);
    }

    if (notifier == _terminateNotify) {
        IOLog("%s::Notification consumer terminated: %s\n", getName(), newService->getName());
        _notificationServices->removeObject(newService);
    }
}

bool AsusSMC::notificationHandler(void * refCon, IOService * newService, IONotifier * notifier) {
    command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &AsusSMC::notificationHandlerGated), newService, notifier);
    return true;
}

void AsusSMC::dispatchMessageGated(int* message, void* data) {
    OSCollectionIterator* i = OSCollectionIterator::withCollection(_notificationServices);

    if (i != NULL) {
        while (IOService* service = OSDynamicCast(IOService, i->getNextObject()))
            service->message(*message, this, data);
        i->release();
    }
}

void AsusSMC::dispatchMessage(int message, void* data) {
    command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &AsusSMC::dispatchMessageGated), &message, data);
}

#pragma mark -
#pragma mark VirtualSMC plugin
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
        reinterpret_cast<const SMC_DATA *>(&lkb), sizeof(lkb), SmcKeyTypeLkb, new SMCKBrdBLightValue(atkDevice),
        SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyLKSS, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&lks), sizeof(lks), SmcKeyTypeLks, nullptr,
        SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyMSLD, vsmcPlugin.data, VirtualSMCAPI::valueWithUint8(0, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));
}

bool AsusSMC::vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier) {
    if (sensors && vsmc) {
        DBGLOG("asld", "got vsmc notification");
        auto self = static_cast<AsusSMC *>(sensors);
        auto ret = vsmc->callPlatformFunction(VirtualSMCAPI::SubmitPlugin, true, sensors, &self->vsmcPlugin, nullptr, nullptr);
        if (ret == kIOReturnSuccess) {
            DBGLOG("asld", "submitted plugin");

            self->workloop = self->getWorkLoop();
            self->poller = IOTimerEventSource::timerEventSource(self, [](OSObject *object, IOTimerEventSource *sender) {
                auto ls = OSDynamicCast(AsusSMC, object);
                if (ls) ls->refreshSensor(true);
            });

            if (!self->poller || !self->workloop) {
                SYSLOG("asld", "failed to create poller or workloop");
                return false;
            }

            if (self->workloop->addEventSource(self->poller) != kIOReturnSuccess) {
                SYSLOG("asld", "failed to add timer event source to workloop");
                return false;
            }

            if (self->poller->setTimeoutMS(SensorUpdateTimeoutMS) != kIOReturnSuccess) {
                SYSLOG("asld", "failed to set timeout");
                return false;
            }

            return true;
        } else if (ret != kIOReturnUnsupported) {
            SYSLOG("asld", "plugin submission failure %X", ret);
        } else {
            DBGLOG("asld", "plugin submission to non vsmc");
        }
    } else {
        SYSLOG("asld", "got null vsmc notification");
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
