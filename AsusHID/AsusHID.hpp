//
//  AsusHID.hpp
//  AsusSMC
//
//  Copyright © 2019 Le Bao Hiep
//

#ifndef AsusHID_hpp
#define AsusHID_hpp

#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hidevent/IOHIDEventService.h>
#include <VirtualSMCSDK/kern_vsmcapi.hpp>

#define FEATURE_KBD_REPORT_ID 0x5a
#define FEATURE_KBD_REPORT_SIZE 16

enum {
    kAddAsusHID = iokit_vendor_specific_msg(8102),
    kDelAsusHID = iokit_vendor_specific_msg(7501),
};

class AsusHID : public IOHIDEventService {
    OSDeclareDefaultStructors(AsusHID)

public:
    virtual bool handleStart(IOService *provider) override;
    virtual void handleStop(IOService *provider) override;

private:
    IOService *asusSMC {nullptr};
    IOHIDDevice* hid_device {nullptr};
    IOHIDInterface* hid_interface {nullptr};

    void handleInterruptReport(AbsoluteTime timestamp, IOMemoryDescriptor* report, IOHIDReportType report_type, UInt32 report_id);

    // Ported from hid-asus driver
    void asus_kbd_init();
    void asus_kbd_backlight_set(uint8_t val);
    void asus_kbd_get_functions(unsigned char *kbd_func);
};
#endif /* AsusHID_hpp */
