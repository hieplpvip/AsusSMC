//
//  KernEventServer.cpp
//  KernEventServer
//
//  Copyright © 2018-2020 Le Bao Hiep. All rights reserved.
//

#include "KernEventServer.hpp"
#include <Headers/kern_util.hpp>

bool KernEventServer::setVendorID(const char *vendorCode) {
    if (KERN_SUCCESS != kev_vendor_code_find(vendorCode, &vendorID)) {
        DBGLOG("kevserver", "setVendorID error");
        return false;
    }
    return true;
}

void KernEventServer::setEventCode(u_int32_t code) {
    eventCode = code;
}

bool KernEventServer::sendMessage(int type, int x, int y) {
    // kernel event message
    struct kev_msg kEventMsg = {0};

    // zero out kernel message
    bzero(&kEventMsg, sizeof(struct kev_msg));

    // set vendor code
    kEventMsg.vendor_code = vendorID;

    // set class
    kEventMsg.kev_class = KEV_ANY_CLASS;

    // set subclass
    kEventMsg.kev_subclass = KEV_ANY_SUBCLASS;

    // set event code
    kEventMsg.event_code = eventCode;

    // type
    kEventMsg.dv[0].data_length = sizeof(int);
    kEventMsg.dv[0].data_ptr = &type;

    // x
    kEventMsg.dv[1].data_length = sizeof(int);
    kEventMsg.dv[1].data_ptr = &x;

    // y
    kEventMsg.dv[2].data_length = sizeof(int);
    kEventMsg.dv[2].data_ptr = &y;

    if (KERN_SUCCESS != kev_msg_post(&kEventMsg)) {
        DBGLOG("kevserver", "sendMessage error");
        return false;
    }
    return true;
}
