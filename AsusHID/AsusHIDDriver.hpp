//
//  AsusHIDDriver.hpp
//  AsusHID
//
//  Copyright © 2019 Le Bao Hiep. All rights reserved.
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

#define AbsoluteTime_to_scalar(x)    (*(uint64_t *)(x))
#define CMP_ABSOLUTETIME(t1, t2)                 \
    (AbsoluteTime_to_scalar(t1) >                \
        AbsoluteTime_to_scalar(t2)? (int)+1 :    \
    (AbsoluteTime_to_scalar(t1) <                \
        AbsoluteTime_to_scalar(t2)? (int)-1 : 0))

enum {
    kAddAsusHIDDriver = iokit_vendor_specific_msg(201),
    kDelAsusHIDDriver = iokit_vendor_specific_msg(202),
    kSleep = iokit_vendor_specific_msg(203),
    kAirplaneMode = iokit_vendor_specific_msg(204),
    kTouchpadToggle = iokit_vendor_specific_msg(205),
    kDisplayOff = iokit_vendor_specific_msg(206),
};

class AsusHIDDriver : public IOHIDEventDriver {
    OSDeclareDefaultStructors(AsusHIDDriver)

public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void handleInterruptReport(AbsoluteTime timeStamp, IOMemoryDescriptor *report, IOHIDReportType reportType, UInt32 reportID) override;
    void dispatchKeyboardEvent(AbsoluteTime timeStamp, UInt32 usagePage, UInt32 usage, UInt32 value, IOOptionBits options = 0) override;

    void setKeyboardBacklight(uint8_t val);

private:
    IOService *_asusSMC {nullptr};
    IOHIDInterface *hid_interface {nullptr};

    uint8_t kbd_func = 0;

    OSArray *customKeyboardElements {nullptr};
    void parseCustomKeyboardElements(OSArray *elementArray);

    // Ported from hid-asus driver
    void asus_kbd_init();
    void asus_kbd_backlight_set(uint8_t val);
    void asus_kbd_get_functions(uint8_t *kbd_func);
};
#endif /* AsusHIDDriver_hpp */
