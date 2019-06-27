//
//  AsusHID.hpp
//  AsusSMC
//
//  Copyright © 2019 Le Bao Hiep
//

#ifndef AsusHID_hpp
#define AsusHID_hpp

#include <IOKit/hid/IOHIDKeys.h>
#include <VirtualSMCSDK/kern_vsmcapi.hpp>

enum {
    kAddAsusHID = iokit_vendor_specific_msg(8102),
    kDelAsusHID = iokit_vendor_specific_msg(7501),
};

class AsusHID : public IOService {
    OSDeclareDefaultStructors(AsusHID)

public:
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;

private:
    IOService *asusSMC {nullptr};
};
#endif /* AsusHID_hpp */
