//
//  AsusHIDInterface.hpp
//  AsusSMC
//
//  Copyright © 2019 Le Bao Hiep
//

#ifndef AsusHIDInterface_hpp
#define AsusHIDInterface_hpp

#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDInterface.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <VirtualSMCSDK/kern_vsmcapi.hpp>

#define FEATURE_KBD_REPORT_ID 0x5a
#define FEATURE_KBD_REPORT_SIZE 16

enum {
    kAddAsusHIDInterface = iokit_vendor_specific_msg(8102),
    kDelAsusHIDInterface = iokit_vendor_specific_msg(7501),
};

class AsusHIDInterface : public IOService {
    OSDeclareDefaultStructors(AsusHIDInterface)

public:
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;

private:
    IOService *asusSMC {nullptr};
    IOHIDDevice* hid_device {nullptr};

    // Ported from hid-asus driver
    void asus_kbd_init();
    void asus_kbd_backlight_set(uint8_t val);
    void asus_kbd_get_functions(unsigned char *kbd_func);
};
#endif /* AsusHIDInterface_hpp */
