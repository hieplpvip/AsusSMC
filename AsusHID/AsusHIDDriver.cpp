//
//  AsusHIDDriver.cpp
//  AsusHID
//
//  Copyright © 2019-2020 Le Bao Hiep. All rights reserved.
//

#include "AsusHIDDriver.hpp"

#define super IOHIDEventDriver
OSDefineMetaClassAndStructors(AsusHIDDriver, IOHIDEventDriver);

bool AsusHIDDriver::start(IOService *provider) {
    if (!super::start(provider)) {
        SYSLOG("hid", "failed to start parent");
        return false;
    }

    hid_interface = OSDynamicCast(IOHIDInterface, provider);
    if (!hid_interface) {
        SYSLOG("hid", "failed to cast IOHIDInterface");
        return false;
    }

    OSArray *elements = hid_interface->createMatchingElements();
    //elements->retain();
    if (elements) {
        parseCustomKeyboardElements(elements);
    }
    OSSafeReleaseNULL(elements);

    setProperty("AsusHIDSupported", true);
    setProperty("Copyright", "Copyright © 2019-2020 Le Bao Hiep. All rights reserved.");

    asus_kbd_init();
    asus_kbd_get_functions();
    setProperty("IsKeyboardBacklightSupported", (SUPPORT_KBD_BACKLIGHT & _kbd_function) ? true : false);

    auto key = OSSymbol::withCString("AsusSMCCore");
    auto dict = propertyMatching(key, kOSBooleanTrue);
    _asusSMC = IOService::waitForMatchingService(dict, 5000000000); // wait for 5 secs
    key->release();
    dict->release();

    if (_asusSMC) {
        _asusSMC->message(kHIDAdd, this);
        DBGLOG("hid", "connected with AsusSMC");
    }

    readyForReports = true;

    return true;
}

void AsusHIDDriver::stop(IOService *provider) {
    if (_asusSMC) {
        _asusSMC->message(kHIDDelete, this);
        DBGLOG("hid", "disconnected with AsusSMC");
    }
    OSSafeReleaseNULL(_asusSMC);
    hid_interface = nullptr;
    super::stop(provider);
}

void AsusHIDDriver::parseCustomKeyboardElements(OSArray *elementArray) {
    customKeyboardElements = OSArray::withCapacity(1);
    for (uint32_t index = 0, count = elementArray->getCount(); index < count; index++) {
        IOHIDElement *element = OSDynamicCast(IOHIDElement, elementArray->getObject(index));
        if (!element || element->getUsage() == 0)
            continue;
        if (element->getType() == kIOHIDElementTypeCollection)
            continue;

        uint32_t usagePage = element->getUsagePage();
        if (usagePage == kHIDPage_AsusVendor || usagePage == kHIDPage_MicrosoftVendor) {
            customKeyboardElements->setObject(element);
        }
    }
    setProperty("CustomKeyboardElements", customKeyboardElements);
}

void AsusHIDDriver::handleInterruptReport(uint64_t timeStamp, IOMemoryDescriptor *report, IOHIDReportType reportType, uint32_t reportID) {
    DBGLOG("hid", "handleInterruptReport: reportLength=%d reportType=%d reportID=%d", report->getLength(), reportType, reportID);

    super::handleInterruptReport(timeStamp, report, reportType, reportID);

    if (!readyForReports || reportType != kIOHIDReportTypeInput)
        return;

    handleKeyboardReportCustom(timeStamp, reportID);
}

void AsusHIDDriver::handleKeyboardReportCustom(uint64_t timeStamp, uint32_t reportID) {
    DBGLOG("hid", "handleKeyboardReportCustom: reportID=%d", reportID);

    if (!customKeyboardElements) {
        DBGLOG("hid", "handleInterruptReport: null customKeyboardElements");
        return;
    }

    for (uint32_t index = 0, count = customKeyboardElements->getCount(); index < count; index++) {
        IOHIDElement *element;
        uint64_t elementTimeStamp;
        uint32_t usagePage, usage, value, preValue;

        element = OSDynamicCast(IOHIDElement, customKeyboardElements->getObject(index));
        if (!element || element->getReportID() != reportID)
            continue;

        elementTimeStamp = element->getTimeStamp();
        if (timeStamp != elementTimeStamp)
            continue;

        usagePage = element->getUsagePage();
        usage     = element->getUsage();

        preValue = element->getValue(kIOHIDValueOptionsFlagPrevious) != 0;

        // Fix for double reports of KBD illumination (credits @black-dragon74)
        if (usagePage == kHIDPage_AsusVendor && (usage == kHIDUsage_AsusVendor_IlluminationUp || usage == kHIDUsage_AsusVendor_IlluminationDown)) {
            value = element->getValue(kIOHIDValueOptionsFlagRelativeSimple) != 0;
        } else {
            value = element->getValue() != 0;
        }

        if (value == preValue)
            continue;

        dispatchKeyboardEvent(timeStamp, usagePage, usage, value);
        return;
    }
}

void AsusHIDDriver::dispatchKeyboardEvent(uint64_t timeStamp, uint32_t usagePage, uint32_t usage, uint32_t value, IOOptionBits options) {
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
                if (value && _asusSMC) _asusSMC->message(kHIDSleep, this);
                break;
            case kHIDUsage_AsusVendor_TouchpadToggle:
                if (value && _asusSMC) _asusSMC->message(kHIDTouchpadToggle, this);
                break;
            case kHIDUsage_AsusVendor_DisplayOff:
                if (value && _asusSMC) _asusSMC->message(kHIDDisplayOff, this);
                break;
        }
    }
    if (usagePage == kHIDPage_MicrosoftVendor) {
        switch (usage) {
            case kHIDUsage_MicrosoftVendor_WLAN:
                if (value && _asusSMC) _asusSMC->message(kHIDAirplaneMode, this);
                break;
            case kHIDUsage_MicrosoftVendor_BrightnessDown:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_BrightnessDown;
                break;
            case kHIDUsage_MicrosoftVendor_BrightnessUp:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_BrightnessUp;
                break;
            case kHIDUsage_MicrosoftVendor_DisplayOff:
                if (value && _asusSMC) _asusSMC->message(kHIDDisplayOff, this);
                break;
        }
    }

    // Fix erratic caps lock key (credits @black-dragon74)
    if (usage == kHIDUsage_KeyboardCapsLock) {
        IOSleep(80);
    }

    DBGLOG("hid", "dispatchKeyboardEvent usagePage=%d usage=%d value=%d", usagePage, usage, value);
    super::dispatchKeyboardEvent(timeStamp, usagePage, usage, value, options);
}

void AsusHIDDriver::setKeyboardBacklight(uint16_t val) {
    asus_kbd_backlight_set(val);
}

void AsusHIDDriver::asus_kbd_init() {
    uint8_t buf[] = { KBD_FEATURE_REPORT_ID, 0x41, 0x53, 0x55, 0x53, 0x20, 0x54, 0x65, 0x63, 0x68, 0x2e, 0x49, 0x6e, 0x63, 0x2e, 0x00 };

    IOBufferMemoryDescriptor *report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut, sizeof(buf));
    if (!report) {
        SYSLOG("hid", "asus_kbd_init: Could not allocate IOBufferMemoryDescriptor");
        return;
    }

    report->writeBytes(0, buf, sizeof(buf));

    hid_interface->setReport(report, kIOHIDReportTypeFeature, KBD_FEATURE_REPORT_ID);

    report->release();
}

void AsusHIDDriver::asus_kbd_get_functions() {
    uint8_t buf[] = { KBD_FEATURE_REPORT_ID, 0x05, 0x20, 0x31, 0x00, 0x08 };

    IOBufferMemoryDescriptor *report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut, sizeof(buf));
    if (!report) {
        SYSLOG("hid", "asus_kbd_get_functions: Could not allocate IOBufferMemoryDescriptor");
        return;
    }

    report->writeBytes(0, buf, sizeof(buf));

    hid_interface->setReport(report, kIOHIDReportTypeFeature, KBD_FEATURE_REPORT_ID);

    hid_interface->getReport(report, kIOHIDReportTypeFeature, KBD_FEATURE_REPORT_ID);

    uint8_t *func = (uint8_t *)IOMalloc(KBD_FEATURE_REPORT_SIZE);

    if (!func) {
        SYSLOG("hid", "asus_kbd_get_functions: Could not allocate memory for reading report");
        goto exit;
    }

    report->prepare();
    report->readBytes(0, func, KBD_FEATURE_REPORT_SIZE);
    report->complete();

    _kbd_function = func[6];

    IOFree(func, KBD_FEATURE_REPORT_SIZE);

exit:
    report->release();
}

void AsusHIDDriver::asus_kbd_backlight_set(uint8_t val) {
    DBGLOG("hid", "asus_kbd_backlight_set: val=%d", val);

    if (!(SUPPORT_KBD_BACKLIGHT & _kbd_function)) {
        DBGLOG("hid", "asus_kbd_backlight_set: Keyboard backlight is not supported");
        return;
    }

    uint8_t buf[] = { KBD_FEATURE_REPORT_ID, 0xba, 0xc5, 0xc4, 0x00 };
    buf[4] = val;

    IOBufferMemoryDescriptor *report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut, sizeof(buf));
    if (!report) {
        SYSLOG("hid", "asus_kbd_backlight_set: Could not allocate IOBufferMemoryDescriptor");
        return;
    }

    report->writeBytes(0, buf, sizeof(buf));

    hid_interface->setReport(report, kIOHIDReportTypeFeature, KBD_FEATURE_REPORT_ID);

    report->release();
}
