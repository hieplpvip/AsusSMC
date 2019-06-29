//
//  BezelServices.h
//  AsusSMCDaemon
//
//  Copyright © 2018-2019 Le Bao Hiep. All rights reserved.
//

#ifndef BezelServices_h
#define BezelServices_h

typedef enum {
    BSGraphicBacklightMeter                         = 0xfffffff7,
    BSGraphicBacklightFailure                       = 0xfffffff6,
    BSGraphicBacklightFailureMessage                = 0xfffffff3,
    BSGraphicBacklightDoubleFailureMessage          = 0xfffffff2,
    BSGraphicKeyboardBacklightMeter                 = 0xfffffff1,
    BSGraphicKeyboardBacklightDisabledMeter         = 0xfffffff0,
    BSGraphicKeyboardBacklightNotConnected          = 0xffffffef,
    BSGraphicKeyboardBacklightDisabledNotConnected  = 0xffffffee,
    BSGraphicMacProOpen                             = 0xffffffe9,
    BSGraphicSpeakerMuted                           = 0xffffffe8,
    BSGraphicSpeaker                                = 0xffffffe7,
    BSGraphicRemoteBattery                          = 0xffffffe6,
    BSGraphicHotspot                                = 0xffffffe5,
    BSGraphicSleep                                  = 0xffffffe3,
    BSGraphicSpeakerDisabledMuted                   = 0xffffffe2,
    BSGraphicSpeakerDisabled                        = 0xffffffe1,
    BSGraphicSpeakerMeter                           = 0xffffffe0,
    BSGraphicNewRemoteBattery                       = 0xffffffcb,
} BSGraphic;

extern void *BSDoGraphicWithMessage(CGDirectDisplayID arg0, BSGraphic arg1, int arg2, const char *arg3, int length);
extern void *BSDoGraphicWithMeterAndTimeout(CGDirectDisplayID arg0, BSGraphic arg1, int arg2, float v, int timeout);

#endif /* BezelServices_h */
