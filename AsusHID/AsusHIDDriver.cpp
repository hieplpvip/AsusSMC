//
//  AsusHIDDriver.cpp
//  AsusHID
//
//  Copyright © 2019 Le Bao Hiep. All rights reserved.
//

#include "AppleHIDUsageTables.h"
#include "AsusHIDDriver.hpp"

#define super IOHIDEventDriver
OSDefineMetaClassAndStructors(AsusHIDDriver, IOHIDEventDriver);

bool AsusHIDDriver::start(IOService *provider) {
    DBGLOG("hid", "start is called");

    if (!super::start(provider)) {
        SYSLOG("hid", "Error loading HID driver");
        return false;
    }

    hid_interface = OSDynamicCast(IOHIDInterface, provider);
    if (!hid_interface)
        return false;

    hid_device = OSDynamicCast(IOHIDDevice, hid_interface->getParentEntry(gIOServicePlane));
    if (!hid_device)
        return false;

    usb_interface = OSDynamicCast(IOUSBHostInterface, hid_device->getParentEntry(gIOServicePlane));
    if (usb_interface) {
        usb_interface->setProperty("AsusHIDSupported", true);
        setProperty("USBInterfaceAvailable", true);
    }

    OSArray *elements = hid_interface->createMatchingElements();
    parseCustomKeyboardElements(elements);

    hid_interface->setProperty("AsusHIDSupported", true);
    hid_device->setProperty("AsusHIDSupported", true);
    setProperty("AsusHIDSupported", true);
    setProperty("Copyright", "Copyright © 2018-2019 Le Bao Hiep. All rights reserved.");

    extern kmod_info_t kmod_info;
    setProperty("AsusSMC-Version", kmod_info.version);
#ifdef DEBUG
    setProperty("AsusSMC-Build", "Debug");
#else
    setProperty("AsusSMC-Build", "Release");
#endif

    asus_kbd_init();
    //uint8_t kbd_func;
    //asus_kbd_get_functions(&kbd_func);

    auto dict = propertyMatching(OSSymbol::withCString("AsusHIDHost"), kOSBooleanTrue);
    _asusSMC = IOService::waitForMatchingService(dict, 5000000000); // wait for 5 secs
    dict->release();

    if (_asusSMC/* && (kbd_func & SUPPORT_KBD_BACKLIGHT)*/) {
        usb_interface->setProperty("SupportKeyboardBacklight", true);
        _asusSMC->message(kAddAsusHIDDriver, this);
        DBGLOG("hid", "Connected with AsusSMC");
    }
    return true;
}

void AsusHIDDriver::stop(IOService *provider) {
    DBGLOG("hid", "stop is called");

    if (_asusSMC) {
        _asusSMC->message(kDelAsusHIDDriver, this);
        DBGLOG("hid", "Disconnected with AsusSMC");
    }
    OSSafeReleaseNULL(_asusSMC);

    hid_interface = nullptr;
    hid_device = nullptr;
    usb_interface = nullptr;

    super::stop(provider);
}

IOReturn AsusHIDDriver::getCtlReport(uint8_t reportID, uint8_t reportType, void* dataBuffer, uint16_t size) {
    StandardUSB::DeviceRequest devReq =
    {
        .bmRequestType = kHIDRqGetReport,
        .bRequest = makeDeviceRequestbmRequestType(kRequestDirectionIn, kRequestTypeClass, kRequestRecipientInterface),
        .wValue = (uint16_t)((reportType << 8) | reportID),
        .wIndex = usb_interface->getInterfaceDescriptor()->bInterfaceNumber,
        .wLength = size
    };
    return (usb_interface) ? usb_interface->deviceRequest(devReq, dataBuffer, NULL) : kIOReturnError;
}

IOReturn AsusHIDDriver::setCtlReport(uint8_t reportID, uint8_t reportType, void* dataBuffer, uint16_t size) {
    StandardUSB::DeviceRequest devReq =
    {
        .bmRequestType = kHIDRqSetReport,
        .bRequest = makeDeviceRequestbmRequestType(kRequestDirectionOut, kRequestTypeClass, kRequestRecipientInterface),
        .wValue = (uint16_t)((reportType << 8) | reportID),
        .wIndex = usb_interface->getInterfaceDescriptor()->bInterfaceNumber,
        .wLength = size
    };
    return (usb_interface) ? usb_interface->deviceRequest(devReq, dataBuffer, NULL) : kIOReturnError;
}

void AsusHIDDriver::parseCustomKeyboardElements(OSArray *elementArray) {
    customKeyboardElements = OSArray::withCapacity(4);
    UInt32 count, index;
    for (index = 0, count = elementArray->getCount(); index < count; index++ ) {
        IOHIDElement *element = OSDynamicCast(IOHIDElement, elementArray->getObject(index));
        if (!element || element->getUsage() == 0)
            continue;
        if (element->getType() == kIOHIDElementTypeCollection)
            continue;

        UInt32 usagePage      = element->getUsagePage();
        UInt32 usage          = element->getUsage();
        bool   store          = false;

        switch (usagePage) {
            case kHIDPage_AsusVendor:
                switch (usage) {
                    case kHIDUsage_AsusVendor_BrightnessDown:
                    case kHIDUsage_AsusVendor_BrightnessUp:
                    case kHIDUsage_AsusVendor_DisplayOff:
                    case kHIDUsage_AsusVendor_ROG:
                    case kHIDUsage_AsusVendor_Power4Gear:
                    case kHIDUsage_AsusVendor_TouchpadToggle:
                    case kHIDUsage_AsusVendor_Sleep:
                    case kHIDUsage_AsusVendor_MicMute:
                    case kHIDUsage_AsusVendor_Camera:
                    case kHIDUsage_AsusVendor_RFKill:
                    case kHIDUsage_AsusVendor_Fan:
                    case kHIDUsage_AsusVendor_Calc:
                    case kHIDUsage_AsusVendor_Splendid:
                    case kHIDUsage_AsusVendor_IlluminationUp:
                    case kHIDUsage_AsusVendor_IlluminationDown:
                        store = true;
                        break;
                }
                break;
            case kHIDPage_MicrosoftVendor:
                switch (usage) {
                    case kHIDUsage_MicrosoftVendor_WLAN:
                    case kHIDUsage_MicrosoftVendor_BrightnessDown:
                    case kHIDUsage_MicrosoftVendor_BrightnessUp:
                    case kHIDUsage_MicrosoftVendor_DisplayOff:
                    case kHIDUsage_MicrosoftVendor_Camera:
                    case kHIDUsage_MicrosoftVendor_ROG:
                        store = true;
                        break;
                }
                break;
        }
        if (store)
            customKeyboardElements->setObject(element);
    }
    setProperty("CustomKeyboardElements", customKeyboardElements);
}

void AsusHIDDriver::handleInterruptReport(AbsoluteTime timeStamp, IOMemoryDescriptor *report, IOHIDReportType reportType, UInt32 reportID) {
    DBGLOG("hid", "handleInterruptReport reportLength=%d reportType=%d reportID=%d", report->getLength(), reportType, reportID);
    UInt32 index, count;
    for (index = 0, count = customKeyboardElements->getCount(); index < count; index++) {
        IOHIDElement * element;
        AbsoluteTime   elementTimeStamp;
        UInt32         usagePage, usage, value, preValue;

        element = OSDynamicCast(IOHIDElement, customKeyboardElements->getObject(index));
        if (!element || element->getReportID() != reportID)
            continue;

        elementTimeStamp = element->getTimeStamp();
        if (CMP_ABSOLUTETIME(&timeStamp, &elementTimeStamp) != 0)
            continue;

        preValue = element->getValue(kIOHIDValueOptionsFlagPrevious) != 0;
        value    = element->getValue() != 0;

        if (value == preValue)
            continue;

        usagePage = element->getUsagePage();
        usage     = element->getUsage();

        dispatchKeyboardEvent(timeStamp, usagePage, usage, value);
        return;
    }
    super::handleInterruptReport(timeStamp, report, reportType, reportID);
}

void AsusHIDDriver::dispatchKeyboardEvent(AbsoluteTime timeStamp, UInt32 usagePage, UInt32 usage, UInt32 value, IOOptionBits options) {
    DBGLOG("hid", "dispatchKeyboardEvent usagePage=%d usage=%d", usagePage, usage);
    if (usagePage == kHIDPage_AsusVendor) {
        switch (usage) {
            case kHIDUsage_AsusVendor_BrightnessDown:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_BrightnessDown;
                break;
            case kHIDUsage_AsusVendor_BrightnessUp:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_BrightnessUp;
                break;
            case kHIDUsage_AsusVendor_IlluminationUp:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_IlluminationUp;
                break;
            case kHIDUsage_AsusVendor_IlluminationDown:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_IlluminationDown;
                break;
            case kHIDUsage_AsusVendor_Sleep:
                if (value && _asusSMC) _asusSMC->message(kSleep, this);
                return;
            case kHIDUsage_AsusVendor_TouchpadToggle:
                if (value && _asusSMC) _asusSMC->message(kTouchpadToggle, this);
                return;
            case kHIDUsage_AsusVendor_DisplayOff:
                if (value && _asusSMC) _asusSMC->message(kDisplayOff, this);
                return;
            default:
                return;
        }
    }
    if (usagePage == kHIDPage_MicrosoftVendor) {
        switch (usage) {
            case kHIDUsage_MicrosoftVendor_WLAN:
                if (value && _asusSMC) _asusSMC->message(kAirplaneMode, this);
                return;
            case kHIDUsage_MicrosoftVendor_BrightnessDown:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_BrightnessDown;
                break;
            case kHIDUsage_MicrosoftVendor_BrightnessUp:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_BrightnessUp;
                break;
            case kHIDUsage_MicrosoftVendor_DisplayOff:
                if (value && _asusSMC) _asusSMC->message(kDisplayOff, this);
                return;
            default:
                break;
        }
    }
    super::dispatchKeyboardEvent(timeStamp, usagePage, usage, value, options);
}

void AsusHIDDriver::setKeyboardBacklight(uint8_t val) {
    asus_kbd_backlight_set(val / 64);
}

#pragma mark -
#pragma mark HID functions ported from Linux
#pragma mark -

void AsusHIDDriver::asus_kbd_init() {
    uint8_t buf[] = { 0x5a, 0x41, 0x53, 0x55, 0x53, 0x20, 0x54,
        0x65, 0x63, 0x68, 0x2e, 0x49, 0x6e, 0x63, 0x2e, 0x00 };
    setCtlReport(FEATURE_KBD_REPORT_ID, kIOHIDReportTypeFeature, buf, sizeof(buf));
}

void AsusHIDDriver::asus_kbd_backlight_set(uint8_t val) {
    DBGLOG("hid", "asus_kbd_backlight_set val=%d", val);
    uint8_t buf[] = { 0x5a, 0xba, 0xc5, 0xc4, 0x00 };
    buf[4] = val;
    IOReturn res = setCtlReport(FEATURE_KBD_REPORT_ID, kIOHIDReportTypeFeature, buf, sizeof(buf));
    if (res == kIOReturnSuccess)
        DBGLOG("hid", "asus_kbd_backlight_set succeeded %d", res);
    else
        DBGLOG("hid", "asus_kbd_backlight_set failed %d", res);
}

void AsusHIDDriver::asus_kbd_get_functions(uint8_t *kbd_func) {
    uint8_t buf[] = { 0x5a, 0x05, 0x20, 0x31, 0x00, 0x08 };
    setCtlReport(FEATURE_KBD_REPORT_ID, kIOHIDReportTypeFeature, buf, sizeof(buf));

    uint8_t *readbuf = (uint8_t *)IOMalloc(FEATURE_KBD_REPORT_SIZE);
    bzero(readbuf, FEATURE_KBD_REPORT_SIZE);
    getCtlReport(FEATURE_KBD_REPORT_ID, kIOHIDReportTypeFeature, buf, sizeof(buf));
    *kbd_func = readbuf[6];
    IOFree(readbuf, FEATURE_KBD_REPORT_SIZE);
}
