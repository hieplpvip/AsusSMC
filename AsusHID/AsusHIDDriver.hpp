//
//  AsusHIDDriver.hpp
//  AsusHID
//
//  Copyright © 2019-2020 Le Bao Hiep. All rights reserved.
//

#ifndef AsusHIDDriver_hpp
#define AsusHIDDriver_hpp

#include <IOKit/hidevent/IOHIDEventDriver.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <VirtualSMCSDK/kern_vsmcapi.hpp>
#include "HIDUsageTables.h"

#define KBD_FEATURE_REPORT_ID 0x5a
#define KBD_FEATURE_REPORT_SIZE 16
#define SUPPORT_KBD_BACKLIGHT 1

enum {
    kHIDAdd = iokit_vendor_specific_msg(201),
    kHIDDelete = iokit_vendor_specific_msg(202),
    kHIDSleep = iokit_vendor_specific_msg(203),
    kHIDAirplaneMode = iokit_vendor_specific_msg(204),
    kHIDTouchpadToggle = iokit_vendor_specific_msg(205),
    kHIDDisplayOff = iokit_vendor_specific_msg(206),
};

class AsusHIDDriver : public IOHIDEventDriver {
    OSDeclareDefaultStructors(AsusHIDDriver)

public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void handleInterruptReport(uint64_t timeStamp, IOMemoryDescriptor *report, IOHIDReportType reportType, uint32_t reportID) override;
    void dispatchKeyboardEvent(uint64_t timeStamp, uint32_t usagePage, uint32_t usage, uint32_t value, IOOptionBits options = 0) override;

    void setKeyboardBacklight(uint16_t val);

private:
    IOService *_asusSMC {nullptr};
    IOHIDInterface *hid_interface {nullptr};

    uint8_t _kbd_function {0};

    bool readyForReports {false};

    OSArray *customKeyboardElements {nullptr};
    void parseCustomKeyboardElements(OSArray *elementArray);
    void handleKeyboardReportCustom(uint64_t timeStamp, uint32_t reportID);

    // Ported from Linux driver hid-asus
    void asus_kbd_init();
    void asus_kbd_get_functions();
    void asus_kbd_backlight_set(uint8_t val);
};
#endif /* AsusHIDDriver_hpp */
