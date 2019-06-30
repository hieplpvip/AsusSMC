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

    hid_device = OSDynamicCast(IOHIDDevice, hid_interface->getParentEntry(gIOServicePlane));
    if (!hid_device)
        return false;

    hid_interface->setProperty("AsusHIDSupported", true);
    hid_device->setProperty("AsusHIDSupported", true);
    setProperty("AsusHIDSupported", true);
    setProperty("Copyright", "Copyright © 2018-2019 Le Bao Hiep. All rights reserved.");

    asus_kbd_init();
    asus_kbd_backlight_set(255);

    auto dict = propertyMatching(OSSymbol::withCString("AsusHIDHost"), kOSBooleanTrue);
    auto asusSMC = IOService::waitForMatchingService(dict, 5000000000); // wait for 5 secs
    dict->release();

    if (asusSMC) {
        asusSMC->message(kAddAsusHIDDriver, this);
        DBGLOG("hid", "Connected with AsusSMC");
    }
    return true;
}

void AsusHIDDriver::stop(IOService *provider) {
    DBGLOG("hid", "stop is called");

    if (asusSMC) {
        asusSMC->message(kDelAsusHIDDriver, this);
        DBGLOG("hid", "Disconnected with AsusSMC");
    }
    OSSafeReleaseNULL(asusSMC);

    hid_interface = nullptr;
    hid_device = nullptr;

    super::stop(provider);
}

void AsusHIDDriver::handleInterruptReport(AbsoluteTime timestamp, IOMemoryDescriptor* report, IOHIDReportType report_type, UInt32 report_id) {
    DBGLOG("hid", "handleInterruptReport is called");
    super::handleInterruptReport(timestamp, report, report_type, report_id);
}

void AsusHIDDriver::dispatchKeyboardEvent(AbsoluteTime timeStamp, UInt32 usagePage, UInt32 usage, UInt32 value, IOOptionBits options) {
    if (usagePage == kHIDPage_AsusVendor) {
        switch (usage) {
            case 0x10:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_BrightnessDown;
                break;
            case 0x20:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_BrightnessUp;
                break;
            case 0xc4:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_IlluminationUp;
                break;
            case 0xc5:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_IlluminationDown;
                break;
            default:
                DBGLOG("hid", "AsusVendor HID UsagePage: unknown usage %d", usage);
                return; // ignore the rest
        }
    }
    if (usagePage == kHIDPage_MicrosoftVendor) {
        switch (usage) {
            case 0xf2:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_BrightnessDown;
                break;
            case 0xf3:
                usagePage = kHIDPage_AppleVendorTopCase;
                usage = kHIDUsage_AV_TopCase_BrightnessUp;
                break;
            default:
                DBGLOG("hid", "MicrosoftVendor HID UsagePage: unknown usage %d", usage);
                break;
        }
    }
    super::dispatchKeyboardEvent(timeStamp, usagePage, usage, value, options);
}

void AsusHIDDriver::setKeyboardBacklight(uint8_t val) {
    DBGLOG("hid", "setKeyboardBacklight val=%d", val);
    asus_kbd_backlight_set(val);
}

#pragma mark -
#pragma mark HID functions ported from Linux
#pragma mark -

void AsusHIDDriver::asus_kbd_init() {
    uint8_t buf[] = { FEATURE_KBD_REPORT_ID, 0x41, 0x53, 0x55, 0x53, 0x20, 0x54,
        0x65, 0x63, 0x68, 0x2e, 0x49, 0x6e, 0x63, 0x2e, 0x00 };
    IOBufferMemoryDescriptor* report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof(buf));
    report->writeBytes(0, &buf, sizeof(buf));
    hid_device->setReport(report, kIOHIDReportTypeFeature, FEATURE_KBD_REPORT_ID);
    report->release();
}

void AsusHIDDriver::asus_kbd_backlight_set(uint8_t val) {
    uint8_t buf[] = { FEATURE_KBD_REPORT_ID, 0xba, 0xc5, 0xc4, val };
    IOBufferMemoryDescriptor* report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof(buf));
    report->writeBytes(0, &buf, sizeof(buf));
    hid_device->setReport(report, kIOHIDReportTypeFeature, FEATURE_KBD_REPORT_ID);
    report->release();
}

void AsusHIDDriver::asus_kbd_get_functions(unsigned char *kbd_func) {
    uint8_t buf[] = { FEATURE_KBD_REPORT_ID, 0x05, 0x20, 0x31, 0x00, 0x08 };
    IOBufferMemoryDescriptor* report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof(buf));
    report->writeBytes(0, &buf, sizeof(buf));
    hid_device->setReport(report, kIOHIDReportTypeFeature, FEATURE_KBD_REPORT_ID);
    report->release();

    report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, FEATURE_KBD_REPORT_SIZE);
    hid_device->getReport(report, kIOHIDReportTypeFeature, FEATURE_KBD_REPORT_ID);
    report->readBytes(6, kbd_func, 1);
    report->release();
}
