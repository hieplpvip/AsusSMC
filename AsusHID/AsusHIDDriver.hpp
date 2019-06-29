//
//  AsusHIDDriver.hpp
//  AsusHID
//
//  Copyright © 2019 Le Bao Hiep. All rights reserved.
//

#ifndef AsusHIDDriver_hpp
#define AsusHIDDriver_hpp

#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDInterface.h>
#include <IOKit/hidevent/IOHIDEventDriver.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <VirtualSMCSDK/kern_vsmcapi.hpp>

#define FEATURE_KBD_REPORT_ID 0x5a
#define FEATURE_KBD_REPORT_SIZE 16

enum {
    kAddAsusHIDDriver = iokit_vendor_specific_msg(8102),
    kDelAsusHIDDriver = iokit_vendor_specific_msg(7501),
};

class AsusHIDDriver : public IOHIDEventDriver {
    OSDeclareDefaultStructors(AsusHIDDriver)

public:
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    void handleInterruptReport(AbsoluteTime timestamp, IOMemoryDescriptor* report, IOHIDReportType report_type, UInt32 report_id) override;

private:
    IOService *asusSMC {nullptr};
    IOHIDDevice* hid_device {nullptr};
    IOHIDInterface* hid_interface {nullptr};

    // Ported from hid-asus driver
    void asus_kbd_init();
    void asus_kbd_backlight_set(uint8_t val);
    void asus_kbd_get_functions(unsigned char *kbd_func);
};
#endif /* AsusHIDDriver_hpp */
