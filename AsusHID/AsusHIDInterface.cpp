//
//  AsusHIDInterface.cpp
//  AsusSMC
//
//  Copyright © 2019 Le Bao Hiep
//

#include "AsusHIDInterface.hpp"

#define super IOService
OSDefineMetaClassAndStructors(AsusHIDInterface, IOService);

bool AsusHIDInterface::start(IOService *provider) {
    DBGLOG("hid", "start is called");

    if (!super::start(provider)) {
        SYSLOG("hid", "Error loading HID driver");
        return false;
    }

    this->attach(provider);

    auto dict = propertyMatching(OSSymbol::withCString("AsusHIDHost"), kOSBooleanTrue);
    auto asusSMC = IOService::waitForMatchingService(dict, 5000000000); // wait for 5 secs
    dict->release();

    if (asusSMC) {
        asusSMC->message(kAddAsusHIDInterface, this);
        DBGLOG("hid", "Connected with AsusSMC");
    }

    hid_device = OSDynamicCast(IOHIDDevice, provider);
    if (!hid_device)
        return false;

    hid_device->setProperty("AsusHIDSupported", true);

    setProperty("Copyright", "Copyright © 2019 hieplpvip");
    setProperty(kIOHIDTransportKey, hid_device->copyProperty(kIOHIDTransportKey));
    setProperty(kIOHIDManufacturerKey, hid_device->copyProperty(kIOHIDManufacturerKey));
    setProperty(kIOHIDProductKey, hid_device->copyProperty(kIOHIDProductKey));
    setProperty(kIOHIDLocationIDKey, hid_device->copyProperty(kIOHIDLocationIDKey));
    setProperty(kIOHIDVersionNumberKey, hid_device->copyProperty(kIOHIDVersionNumberKey));
    return true;
}

void AsusHIDInterface::stop(IOService *provider) {
    DBGLOG("hid", "handleStop is called");

    if (asusSMC) {
        asusSMC->message(kDelAsusHIDInterface, this);
        DBGLOG("hid", "Disconnected with AsusSMC");
    }
    OSSafeReleaseNULL(asusSMC);

    hid_device = nullptr;

    super::stop(provider);
}

#pragma mark -
#pragma mark HID functions ported from Linux
#pragma mark -

void AsusHIDInterface::asus_kbd_init() {
    uint8_t buf[] = { FEATURE_KBD_REPORT_ID, 0x41, 0x53, 0x55, 0x53, 0x20, 0x54,
        0x65, 0x63, 0x68, 0x2e, 0x49, 0x6e, 0x63, 0x2e, 0x00 };
    IOBufferMemoryDescriptor* report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof(buf));
    report->writeBytes(0, &buf, sizeof(buf));
    hid_device->setReport(report, kIOHIDReportTypeFeature, FEATURE_KBD_REPORT_ID);
    report->release();
}

void AsusHIDInterface::asus_kbd_backlight_set(uint8_t val) {
    uint8_t buf[] = { FEATURE_KBD_REPORT_ID, 0xba, 0xc5, 0xc4, val };
    IOBufferMemoryDescriptor* report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof(buf));
    report->writeBytes(0, &buf, sizeof(buf));
    hid_device->setReport(report, kIOHIDReportTypeFeature, FEATURE_KBD_REPORT_ID);
    report->release();
}

void AsusHIDInterface::asus_kbd_get_functions(unsigned char *kbd_func) {
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
