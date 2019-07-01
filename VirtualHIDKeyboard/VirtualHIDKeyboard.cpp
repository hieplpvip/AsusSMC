#include "VirtualHIDKeyboard.hpp"

#define super IOHIDDevice
OSDefineMetaClassAndStructors(VirtualHIDKeyboard, super);

const uint8_t reportDescriptor_[] = {
    0x05, 0x0c,       // Usage Page (Consumer)
    0x09, 0x01,       // Usage 1 (kHIDUsage_Csmr_ConsumerControl)
    0xa1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report Id (1)
    0x05, 0x0c,       //   Usage Page (Consumer)
    0x95, 0x20,       //   Report Count............ (32)
    0x75, 0x08,       //   Report Size............. (8)
    0x15, 0x00,       //   Logical Minimum......... (0)
    0x26, 0xff, 0x00, //   Logical Maximum......... (255)
    0x19, 0x00,       //   Usage Minimum........... (0)
    0x29, 0xff,       //   Usage Maximum........... (255)
    0x81, 0x00,       //   Input...................(Data, Array, Absolute)
    0xc0,             // End Collection

    0x06, 0x00, 0xff, // Usage Page (kHIDPage_AppleVendor)
    0x09, 0x01,       // Usage 1 (kHIDUsage_AppleVendor_TopCase)
    0xa1, 0x01,       // Collection (Application)
    0x85, 0x02,       //   Report Id (2)
    0x05, 0xff,       //   Usage Page (kHIDPage_AppleVendorTopCase)
    0x95, 0x20,       //   Report Count............ (32)
    0x75, 0x08,       //   Report Size............. (8)
    0x15, 0x00,       //   Logical Minimum......... (0)
    0x26, 0xff, 0x00, //   Logical Maximum......... (255)
    0x19, 0x00,       //   Usage Minimum........... (0)
    0x29, 0xff,       //   Usage Maximum........... (255)
    0x81, 0x00,       //   Input...................(Data, Array, Absolute)
    0xc0,             // End Collection
};

int countryCode_;

bool VirtualHIDKeyboard::handleStart(IOService *provider) {
    if (!super::handleStart(provider)) {
        return false;
    }

    setProperty("HIDDefaultBehavior", kOSBooleanTrue);
    setProperty("AppleVendorSupported", kOSBooleanTrue);

    return true;
}

OSNumber *VirtualHIDKeyboard::newCountryCodeNumber() const {
    return OSNumber::withNumber(static_cast<uint32_t>(countryCode_), 32);
}

IOReturn VirtualHIDKeyboard::newReportDescriptor(IOMemoryDescriptor **descriptor) const {
    *descriptor = IOBufferMemoryDescriptor::withBytes(reportDescriptor_, sizeof(reportDescriptor_), kIODirectionNone);
    return kIOReturnSuccess;
}

void VirtualHIDKeyboard::setCountryCode(uint8_t value) {
    countryCode_ = value;
}
