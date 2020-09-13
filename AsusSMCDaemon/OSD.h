//
//  OSD.h
//  AsusSMCDaemon
//
//  Copyright © 2018-2020 Le Bao Hiep. All rights reserved.
//

#ifndef OSD_h
#define OSD_h

typedef enum {
    OSDGraphicBacklight                              = 1, // 1, 2, 7, 8
    OSDGraphicSpeaker                                = 3, // 3, 5, 17, 23
    OSDGraphicSpeakerMuted                           = 4, // 4, 16, 21, 22
    OSDGraphicEject                                  = 6,
    OSDGraphicNoWiFi                                 = 9,
    OSDGraphicKeyboardBacklightMeter                 = 11, // 11, 25
    OSDGraphicKeyboardBacklightDisabledMeter         = 12, // 12, 26
    OSDGraphicKeyboardBacklightNotConnected          = 13, // 13, 27
    OSDGraphicKeyboardBacklightDisabledNotConnected  = 14, // 14, 28
    OSDGraphicMacProOpen                             = 15,
    OSDGraphicHotspot                                = 19,
    OSDGraphicSleep                                  = 20,
    // There may be more
} OSDGraphic;

typedef enum {
    OSDPriorityDefault = 0x1f4
} OSDPriority;

@interface OSDManager : NSObject
+ (instancetype)sharedManager;
- (void)showImage:(OSDGraphic)image onDisplayID:(CGDirectDisplayID)display priority:(OSDPriority)priority msecUntilFade:(int)timeout;
- (void)showImage:(OSDGraphic)image onDisplayID:(CGDirectDisplayID)display priority:(OSDPriority)priority msecUntilFade:(int)timeout withText:(NSString *)text;
- (void)showImage:(OSDGraphic)image onDisplayID:(CGDirectDisplayID)display priority:(OSDPriority)priority msecUntilFade:(int)timeout filledChiclets:(int)filled totalChiclets:(int)total locked:(BOOL)locked;
@end

#endif /* OSD_h */
