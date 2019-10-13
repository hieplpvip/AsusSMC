//
//  AsusHIDDriver.cpp
//  AsusHID
//
//  Copyright © 2019 Le Bao Hiep. All rights reserved.
//

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

    OSArray *elements = hid_interface->createMatchingElements();
    if (elements) parseCustomKeyboardElements(elements);
    OSSafeReleaseNULL(elements);

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

    auto key = OSSymbol::withCString("AsusSMCCore");
    auto dict = propertyMatching(key, kOSBooleanTrue);
    _asusSMC = IOService::waitForMatchingService(dict, 5000000000); // wait for 5 secs
    key->release();
    dict->release();

    if (_asusSMC) {
        setProperty("KeyboardBacklightSupported", true);
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
    super::stop(provider);
}

void AsusHIDDriver::parseCustomKeyboardElements(OSArray *elementArray) {
    customKeyboardElements = OSArray::withCapacity(4);
    UInt32 count, index;
    for (index = 0, count = elementArray->getCount(); index < count; index++) {
        IOHIDElement *element = OSDynamicCast(IOHIDElement, elementArray->getObject(index));
        if (!element || element->getUsage() == 0)
            continue;
        if (element->getType() == kIOHIDElementTypeCollection)
            continue;

        UInt32 usagePage = element->getUsagePage();
        UInt32 usage     = element->getUsage();
        bool   store     = false;

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
        IOHIDElement *element;
        AbsoluteTime  elementTimeStamp;
        UInt32        usagePage, usage, value, preValue;

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
#pragma mark Ported from hid-asus.c
#pragma mark -

void AsusHIDDriver::asus_kbd_init() {
    uint8_t buf[] = { KBD_FEATURE_REPORT_ID, 0x41, 0x53, 0x55, 0x53, 0x20, 0x54, 0x65, 0x63, 0x68, 0x2e, 0x49, 0x6e, 0x63, 0x2e, 0x00 };
    OSData* dat = OSData::withBytes(buf, KBD_FEATURE_REPORT_SIZE);
    IOBufferMemoryDescriptor* report = IOBufferMemoryDescriptor::withBytes(dat->getBytesNoCopy(0, KBD_FEATURE_REPORT_SIZE), dat->getLength(), kIODirectionInOut);
    hid_interface->setReport(report, kIOHIDReportTypeFeature, KBD_FEATURE_REPORT_ID);
    dat->release();
    report->release();
}

void AsusHIDDriver::asus_kbd_backlight_set(uint8_t val) {
    DBGLOG("hid", "asus_kbd_backlight_set val=%d", val);
    uint8_t buf[] = { KBD_FEATURE_REPORT_ID, 0xba, 0xc5, 0xc4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    buf[4] = val;
    OSData* dat = OSData::withBytes(buf, KBD_FEATURE_REPORT_SIZE);
    IOBufferMemoryDescriptor* report = IOBufferMemoryDescriptor::withBytes(dat->getBytesNoCopy(0, KBD_FEATURE_REPORT_SIZE), dat->getLength(), kIODirectionInOut);
    hid_interface->setReport(report, kIOHIDReportTypeFeature, KBD_FEATURE_REPORT_ID);
    dat->release();
    report->release();
}

void AsusHIDDriver::asus_kbd_get_functions(uint8_t *kbd_func) {
    uint8_t buf[] = { KBD_FEATURE_REPORT_ID, 0x05, 0x20, 0x31, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    OSData* dat = OSData::withBytes(buf, KBD_FEATURE_REPORT_SIZE);
    IOBufferMemoryDescriptor* report = IOBufferMemoryDescriptor::withBytes(dat->getBytesNoCopy(0, KBD_FEATURE_REPORT_SIZE), dat->getLength(), kIODirectionInOut);
    hid_interface->setReport(report, kIOHIDReportTypeFeature, KBD_FEATURE_REPORT_ID);
    hid_interface->getReport(report, kIOHIDReportTypeFeature, KBD_FEATURE_REPORT_ID);
    uint8_t readbuf[KBD_FEATURE_REPORT_SIZE] = {};
    report->readBytes(0, &readbuf, KBD_FEATURE_REPORT_SIZE);
    *kbd_func = readbuf[6];
    dat->release();
    report->release();
}
