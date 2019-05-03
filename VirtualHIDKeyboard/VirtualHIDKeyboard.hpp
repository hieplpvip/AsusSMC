#include <IOKit/hid/IOHIDDevice.h>

class VirtualHIDKeyboard final : public IOHIDDevice {
    OSDeclareDefaultStructors(VirtualHIDKeyboard);

public:
    virtual bool handleStart(IOService *provider) override;

    // ----------------------------------------

    virtual OSString *newManufacturerString() const override {
        return OSString::withCString("ASUSTeK Computer Inc.");
    }

    virtual OSString *newProductString() const override {
        return OSString::withCString("VirtualHIDKeyboard");
    }

    virtual OSString *newSerialNumberString() const override {
        return OSString::withCString("ASUSSMC2019");
    }

    // ----------------------------------------

    virtual OSNumber *newVendorIDNumber() const override {
        return OSNumber::withNumber(static_cast<uint32_t>(0x16c0), 32);
    }

    virtual OSNumber *newProductIDNumber() const override {
        return OSNumber::withNumber(static_cast<uint32_t>(0x27db), 32);
    }

    virtual OSNumber *newLocationIDNumber() const override {
        return OSNumber::withNumber(static_cast<uint32_t>(0), 32);
    }

    virtual OSNumber *newCountryCodeNumber() const override;

    // ----------------------------------------

    virtual OSNumber *newPrimaryUsagePageNumber() const override {
        return OSNumber::withNumber(static_cast<uint32_t>(kHIDPage_GenericDesktop), 32);
    }

    virtual OSNumber *newPrimaryUsageNumber() const override {
        return OSNumber::withNumber(static_cast<uint32_t>(kHIDUsage_GD_Keyboard), 32);
    }

    // ----------------------------------------

    virtual IOReturn newReportDescriptor(IOMemoryDescriptor **descriptor) const override;

    // ----------------------------------------

    static void setCountryCode(uint8_t value);
};
