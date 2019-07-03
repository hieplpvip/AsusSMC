#include <IOKit/hid/IOHIDDevice.h>

class VirtualHIDKeyboard final : public IOHIDDevice {
    OSDeclareDefaultStructors(VirtualHIDKeyboard);

public:
    bool handleStart(IOService *provider) override;

    // ----------------------------------------

    OSString *newManufacturerString() const override {
        return OSString::withCString("ASUSTeK Computer Inc.");
    }

    OSString *newProductString() const override {
        return OSString::withCString("VirtualHIDKeyboard");
    }

    OSString *newSerialNumberString() const override {
        return OSString::withCString("ASUSSMC2019");
    }

    // ----------------------------------------

    OSNumber *newVendorIDNumber() const override {
        return OSNumber::withNumber(static_cast<uint32_t>(0x16c0), 32);
    }

    OSNumber *newProductIDNumber() const override {
        return OSNumber::withNumber(static_cast<uint32_t>(0x27db), 32);
    }

    OSNumber *newLocationIDNumber() const override {
        return OSNumber::withNumber(static_cast<uint32_t>(0), 32);
    }

    OSNumber *newCountryCodeNumber() const override;

    // ----------------------------------------

    OSNumber *newPrimaryUsagePageNumber() const override {
        return OSNumber::withNumber(static_cast<uint32_t>(kHIDPage_Consumer), 32);
    }

    OSNumber *newPrimaryUsageNumber() const override {
        return OSNumber::withNumber(static_cast<uint32_t>(kHIDUsage_Csmr_ConsumerControl), 32);
    }

    // ----------------------------------------

    IOReturn newReportDescriptor(IOMemoryDescriptor **descriptor) const override;

    // ----------------------------------------

    static void setCountryCode(uint8_t value);
};
