//
//  AsusHID.cpp
//  AsusSMC
//
//  Copyright © 2019 Le Bao Hiep
//

#include "AsusHID.hpp"

#define super IOService
OSDefineMetaClassAndStructors(AsusHID, IOHIDEventService);

bool AsusHID::handleStart(IOService *provider) {
    DBGLOG("hid", "handleStart is called");

    if (!super::start(provider)) {
        SYSLOG("hid", "Error loading HID driver");
        return false;
    }

    this->attach(provider);

    auto dict = propertyMatching(OSSymbol::withCString("AsusHID Supported"), kOSBooleanTrue);
    auto asusSMC = IOService::waitForMatchingService(dict, 5000000000); // wait for 5 secs
    dict->release();

    if (asusSMC) {
        asusSMC->message(kAddAsusHID, this);
        DBGLOG("hid", "Connected with AsusSMC");
    }

    hid_interface = OSDynamicCast(IOHIDInterface, provider);
    if (!hid_interface)
        return false;

    if (!hid_interface->open(this, 0, OSMemberFunctionCast(IOHIDInterface::InterruptReportAction, this, &AsusHID::handleInterruptReport), NULL))
        return false;

    hid_device = OSDynamicCast(IOHIDDevice, hid_interface->getParentEntry(gIOServicePlane));
    if (!hid_device)
        return false;

    setProperty("Copyright", "Copyright © 2019 hieplpvip");
    setProperty(kIOHIDTransportKey, hid_interface->copyProperty(kIOHIDTransportKey));
    setProperty(kIOHIDManufacturerKey, hid_interface->copyProperty(kIOHIDManufacturerKey));
    setProperty(kIOHIDProductKey, hid_interface->copyProperty(kIOHIDProductKey));
    setProperty(kIOHIDLocationIDKey, hid_interface->copyProperty(kIOHIDLocationIDKey));
    setProperty(kIOHIDVersionNumberKey, hid_interface->copyProperty(kIOHIDVersionNumberKey));
    return true;
}

void AsusHID::handleStop(IOService *provider) {
    DBGLOG("hid", "handleStop is called");

    if (asusSMC) {
        asusSMC->message(kDelAsusHID, this);
        DBGLOG("hid", "Disconnected with AsusSMC");
    }
    OSSafeReleaseNULL(asusSMC);

    hid_interface->close(this);

    super::stop(provider);
}

void AsusHID::handleInterruptReport(AbsoluteTime timestamp, IOMemoryDescriptor* report, IOHIDReportType report_type, UInt32 report_id) {
    DBGLOG("hid", "handleInterruptReport is called");
}

#pragma mark -
#pragma mark HID functions ported from Linux
#pragma mark -

void AsusHID::asus_kbd_init() {
    uint8_t buf[] = { FEATURE_KBD_REPORT_ID, 0x41, 0x53, 0x55, 0x53, 0x20, 0x54,
        0x65, 0x63, 0x68, 0x2e, 0x49, 0x6e, 0x63, 0x2e, 0x00 };
    IOBufferMemoryDescriptor* report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof(buf));
    report->writeBytes(0, &buf, sizeof(buf));
    hid_interface->setReport(report, kIOHIDReportTypeFeature, FEATURE_KBD_REPORT_ID);
    report->release();
}

void AsusHID::asus_kbd_backlight_set(uint8_t val) {
    uint8_t buf[] = { FEATURE_KBD_REPORT_ID, 0xba, 0xc5, 0xc4, val };
    IOBufferMemoryDescriptor* report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof(buf));
    report->writeBytes(0, &buf, sizeof(buf));
    hid_interface->setReport(report, kIOHIDReportTypeFeature, FEATURE_KBD_REPORT_ID);
    report->release();
}

void AsusHID::asus_kbd_get_functions(unsigned char *kbd_func) {
    uint8_t buf[] = { FEATURE_KBD_REPORT_ID, 0x05, 0x20, 0x31, 0x00, 0x08 };
    IOBufferMemoryDescriptor* report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, sizeof(buf));
    report->writeBytes(0, &buf, sizeof(buf));
    hid_interface->setReport(report, kIOHIDReportTypeFeature, FEATURE_KBD_REPORT_ID);
    report->release();

    report = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, FEATURE_KBD_REPORT_SIZE);
    hid_interface->getReport(report, kIOHIDReportTypeFeature, FEATURE_KBD_REPORT_ID);
    report->readBytes(6, kbd_func, 1);
    report->release();
}
