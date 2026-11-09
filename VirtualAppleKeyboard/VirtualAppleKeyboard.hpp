//
//  VirtualAppleKeyboard.hpp
//  VirtualAppleKeyboard
//
//  Copyright © 2018-2020 Le Bao Hiep. All rights reserved.
//

#include <IOKit/hid/IOHIDDevice.h>

// Invoked when macOS writes to the keyboard backlight collection.
typedef void (*VAKBacklightSink)(const uint8_t *bytes, uint32_t length, void *context);

class VirtualAppleKeyboard final : public IOHIDDevice {
    OSDeclareDefaultStructors(VirtualAppleKeyboard);

public:
    bool handleStart(IOService *provider) override;

    IOReturn newReportDescriptor(IOMemoryDescriptor **descriptor) const override;

    IOReturn getReport(IOMemoryDescriptor *report, IOHIDReportType reportType, IOOptionBits options) override;

    // The one half of the backlight protocol that was missing. IOHIDDevice's
    // default returns kIOReturnUnsupported, which is why macOS offered the
    // keyboard brightness control but left it disabled: it could read the
    // capability from getReport but every attempt to set a level failed.
    IOReturn setReport(IOMemoryDescriptor *report, IOHIDReportType reportType, IOOptionBits options) override;

    // Wires the collection to the driver that can actually reach the EC.
    static void setBacklightSink(VAKBacklightSink sink, void *context) {
        backlightSink = sink;
        backlightContext = context;
    }

    OSString *newManufacturerString() const override;
    OSString *newProductString() const override;
    OSNumber *newVendorIDNumber() const override;
    OSNumber *newProductIDNumber() const override;
    OSNumber *newLocationIDNumber() const override;
    OSNumber *newCountryCodeNumber() const override;
    OSNumber *newVersionNumber() const override;
    OSNumber *newPrimaryUsagePageNumber() const override;
    OSNumber *newPrimaryUsageNumber() const override;

private:
    static VAKBacklightSink backlightSink;
    static void *backlightContext;

public:
    // Level reported back to macOS in the backlight status blob, on the 0x20
    // scale of the shipped layout. Updated by the owning driver.
    static uint8_t reportedLevel;
    // Request counters. Whether macOS ever reads or writes this collection,
    // and with which report type, decides the whole approach - so it is
    // measured instead of assumed.
    static uint32_t getReportCount;
    static uint32_t setReportCount;
    // Fixed-size request history. Stored as OSData properties of 25 bytes:
    // 24 bytes = 8 (id, type, len) triples, byte 25 = number of triples.
    // Deliberately NOT an OSArray: the OSArray version returned a temporary
    // smart pointer from withCapacity() and crashed the boot.
    uint8_t getLogBytes[25] = {};
    uint8_t setLogBytes[25] = {};
    uint32_t getLogCount = 0;
    uint32_t setLogCount = 0;
};
