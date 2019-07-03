//
//  AsusHIDDriver.hpp
//  AsusHID
//
//  Copyright © 2019 Le Bao Hiep. All rights reserved.
//

#ifndef AsusHIDDriver_hpp
#define AsusHIDDriver_hpp

#include <IOKit/usb/IOUSBHostHIDDevice.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hidevent/IOHIDEventDriver.h>
#include <VirtualSMCSDK/kern_vsmcapi.hpp>

#define FEATURE_KBD_REPORT_ID 0x5a
#define FEATURE_KBD_REPORT_SIZE 16
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

/* Usage Pages */
enum {
    kHIDPage_MicrosoftVendor = 0xff00,
    kHIDPage_AsusVendor      = 0xff31,
};

/* MicrosoftVendor Page (0xff31) */
enum {
    kHIDUsage_MicrosoftVendor_WLAN           = 0xf1,
    kHIDUsage_MicrosoftVendor_BrightnessDown = 0xf2,
    kHIDUsage_MicrosoftVendor_BrightnessUp   = 0xf3,
    kHIDUsage_MicrosoftVendor_DisplayOff     = 0xf4,
    kHIDUsage_MicrosoftVendor_Camera         = 0xf7,
    kHIDUsage_MicrosoftVendor_ROG            = 0xf8,
};

/* AsusVendor Page (0xff31) */
enum {
    kHIDUsage_AsusVendor_BrightnessDown   = 0x10,
    kHIDUsage_AsusVendor_BrightnessUp     = 0x20,
    kHIDUsage_AsusVendor_DisplayOff       = 0x35,
    kHIDUsage_AsusVendor_ROG              = 0x38,
    kHIDUsage_AsusVendor_Power4Gear       = 0x5c, /* Fn+Space Power4Gear Hybrid */
    kHIDUsage_AsusVendor_TouchpadToggle   = 0x6b,
    kHIDUsage_AsusVendor_Sleep            = 0x6c,
    kHIDUsage_AsusVendor_MicMute          = 0x7c,
    kHIDUsage_AsusVendor_Camera           = 0x82,
    kHIDUsage_AsusVendor_RFKill           = 0x88,
    kHIDUsage_AsusVendor_Fan              = 0x99, /* Fn+F5 "fan" symbol on FX503VD */
    kHIDUsage_AsusVendor_Calc             = 0xb5,
    kHIDUsage_AsusVendor_Splendid         = 0xba, /* Fn+C ASUS Splendid */
    kHIDUsage_AsusVendor_IlluminationUp   = 0xc4,
    kHIDUsage_AsusVendor_IlluminationDown = 0xc5,
};

class AsusHIDDriver : public IOHIDEventDriver {
    OSDeclareDefaultStructors(AsusHIDDriver)

public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void handleInterruptReport(AbsoluteTime timeStamp, IOMemoryDescriptor *report, IOHIDReportType reportType, UInt32 reportID) override;
    void dispatchKeyboardEvent(AbsoluteTime timeStamp, UInt32 usagePage, UInt32 usage, UInt32 value, IOOptionBits options = 0) override;

    IOReturn getCtlReport(uint8_t reportID, uint8_t reportType, void* dataBuffer, uint16_t size);
    IOReturn setCtlReport(uint8_t reportID, uint8_t reportType, void* dataBuffer, uint16_t size);

    void setKeyboardBacklight(uint8_t val);

private:
    IOService *_asusSMC {nullptr};
    IOHIDInterface* hid_interface {nullptr};
    IOHIDDevice* hid_device {nullptr};
    IOUSBHostInterface* usb_interface {nullptr};

    OSArray *customKeyboardElements {nullptr};
    void parseCustomKeyboardElements(OSArray* elementArray);

    // Ported from hid-asus driver
    void asus_kbd_init();
    void asus_kbd_backlight_set(uint8_t val);
    void asus_kbd_get_functions(uint8_t *kbd_func);
};
#endif /* AsusHIDDriver_hpp */
