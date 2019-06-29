//
//  main.m
//  AsusSMC
//
//  Copyright © 2018-2019 Le Bao Hiep. All rights reserved.
//

#define AsusSMCEventCode 0x8102

#import <Cocoa/Cocoa.h>
#import <CoreWLAN/CoreWLAN.h>
#import <CoreServices/CoreServices.h>
#import <sys/ioctl.h>
#import <sys/socket.h>
#import <sys/kern_event.h>
#import "BezelServices.h"
#import "OSD.h"
#include <dlfcn.h>

/*
 *    kAERestart        will cause system to restart
 *    kAEShutDown       will cause system to shutdown
 *    kAEReallyLogout   will cause system to logout
 *    kAESleep          will cause system to sleep
 */
extern OSStatus MDSendAppleEventToSystemProcess(AEEventID eventToSend);

// requires IOBluetooth.framework
void IOBluetoothPreferenceSetControllerPowerState(int);
int IOBluetoothPreferenceGetControllerPowerState(void);

static void *(*_BSDoGraphicWithMeterAndTimeout)(CGDirectDisplayID arg0, BSGraphic arg1, int arg2, float v, int timeout) = NULL;

enum {
    kevKeyboardBacklight = 1,
    kevAirplaneMode = 2,
    kevSleep = 3,
    kevTouchpad = 4,
};

struct AsusSMCMessage {
    int type;
    int x;
    int y;
};

const int kMaxDisplays = 16;
u_int32_t vendorID = 0;

bool _loadBezelServices() {
    // Load BezelServices framework
    void *handle = dlopen("/System/Library/PrivateFrameworks/BezelServices.framework/Versions/A/BezelServices", RTLD_GLOBAL);
    if (!handle) {
        NSLog(@"Error opening framework");
        return NO;
    } else {
        _BSDoGraphicWithMeterAndTimeout = dlsym(handle, "BSDoGraphicWithMeterAndTimeout");
        return _BSDoGraphicWithMeterAndTimeout != NULL;
    }
}

bool _loadOSDFramework() {
    return [[NSBundle bundleWithPath:@"/System/Library/PrivateFrameworks/OSD.framework"] load];
}

OSStatus MDSendAppleEventToSystemProcess(AEEventID eventToSendID) {
    AEAddressDesc targetDesc;
    static const ProcessSerialNumber kPSNOfSystemProcess = {0, kSystemProcess };
    AppleEvent eventReply = {typeNull, NULL};
    AppleEvent eventToSend = {typeNull, NULL};

    OSStatus status = AECreateDesc(typeProcessSerialNumber,
                                   &kPSNOfSystemProcess, sizeof(kPSNOfSystemProcess), &targetDesc);

    if (status != noErr) return status;

    status = AECreateAppleEvent(kCoreEventClass, eventToSendID,
                                &targetDesc, kAutoGenerateReturnID, kAnyTransactionID, &eventToSend);

    AEDisposeDesc(&targetDesc);

    if (status != noErr) return status;

    status = AESendMessage(&eventToSend, &eventReply,
                           kAENormalPriority, kAEDefaultTimeout);

    AEDisposeDesc(&eventToSend);
    if (status != noErr) return status;
    AEDisposeDesc(&eventReply);
    return status;
}

CGDirectDisplayID getMainDisplay() {
    CGDirectDisplayID display[kMaxDisplays];
    CGDisplayCount numDisplays;
    CGDisplayErr err;
    err = CGGetOnlineDisplayList(kMaxDisplays, display, &numDisplays);
    if (err != CGDisplayNoErr) {
        printf("cannot get list of displays (error %d)\n", err);
        return 0;
    }
    for (CGDisplayCount i = 0; i < numDisplays; ++i) {
        CGDirectDisplayID dspy = display[i];
        CGDisplayModeRef mode = CGDisplayCopyDisplayMode(dspy);
        if (!mode)
            continue;
        if (CGDisplayIsMain(dspy))
            return dspy;
    }
    return 0;
}

void showBezelServices(BSGraphic image, float filled) {
    //CGDirectDisplayID currentDisplayId = getMainDisplay();
    CGDirectDisplayID currentDisplayId = [NSScreen.mainScreen.deviceDescription [@"NSScreenNumber"] unsignedIntValue];
    _BSDoGraphicWithMeterAndTimeout(currentDisplayId, image, 0x0, filled, 1);
}

void showOSD(OSDGraphic image, int filled, int total) {
    //CGDirectDisplayID currentDisplayId = getMainDisplay();
    CGDirectDisplayID currentDisplayId = [NSScreen.mainScreen.deviceDescription [@"NSScreenNumber"] unsignedIntValue];
    [[NSClassFromString(@"OSDManager") sharedManager] showImage:image onDisplayID:currentDisplayId priority:OSDPriorityDefault msecUntilFade:1000 filledChiclets:filled totalChiclets:total locked:NO];
}

void showKBoardBLightStatus(int level, int max) {
    if (_BSDoGraphicWithMeterAndTimeout != NULL) {
        // El Capitan and probably older systems
        if (level)
            showBezelServices(BSGraphicKeyboardBacklightMeter, (float)level/max);
        else
            showBezelServices(BSGraphicKeyboardBacklightDisabledMeter, 0);
    } else {
        // Sierra+
        if (level)
            showOSD(OSDGraphicKeyboardBacklightMeter, level, max);
        else
            showOSD(OSDGraphicKeyboardBacklightDisabledMeter, level, max);
    }
}

void goToSleep() {
    if (_BSDoGraphicWithMeterAndTimeout != NULL) // El Capitan and probably older systems
        MDSendAppleEventToSystemProcess(kAESleep);
    else {
        // Sierra+
        CGDirectDisplayID currentDisplayId = [NSScreen.mainScreen.deviceDescription [@"NSScreenNumber"] unsignedIntValue];
        [[NSClassFromString(@"OSDManager") sharedManager] showImage:OSDGraphicSleep onDisplayID:currentDisplayId priority:OSDPriorityDefault msecUntilFade:1000];
    }
}

BOOL airplaneModeEnabled = NO, lastWifiState;
int lastBluetoothState;
void toggleAirplaneMode() {
    airplaneModeEnabled = !airplaneModeEnabled;

    CWInterface *currentInterface = [CWWiFiClient.sharedWiFiClient interface];
    NSError *err = nil;

    if (airplaneModeEnabled) {
        lastWifiState = currentInterface.powerOn;
        lastBluetoothState = IOBluetoothPreferenceGetControllerPowerState();
        [currentInterface setPower:NO error:&err];
        IOBluetoothPreferenceSetControllerPowerState(0);
    } else {
        [currentInterface setPower:lastWifiState error:&err];
        IOBluetoothPreferenceSetControllerPowerState(lastBluetoothState);
    }
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        printf("daemon started...\n");

        if (!_loadBezelServices()) {
            _loadOSDFramework();
        }

        //system socket
        int systemSocket = -1;

        //create system socket to receive kernel event data
        systemSocket = socket(PF_SYSTEM, SOCK_RAW, SYSPROTO_EVENT);

        //struct for vendor code
        // ->set via call to ioctl/SIOCGKEVVENDOR
        struct kev_vendor_code vendorCode = {0};

        //set vendor name string
        strncpy(vendorCode.vendor_string, "com.hieplpvip", KEV_VENDOR_CODE_MAX_STR_LEN);

        //get vendor name -> vendor code mapping
        // ->vendor id, saved in 'vendorCode' variable
        ioctl(systemSocket, SIOCGKEVVENDOR, &vendorCode);

        //struct for kernel request
        // ->set filtering options
        struct kev_request kevRequest = {0};

        //init filtering options
        // ->only interested in objective-see's events kevRequest.vendor_code = vendorCode.vendor_code;

        //...any class
        kevRequest.kev_class = KEV_ANY_CLASS;

        //...any subclass
        kevRequest.kev_subclass = KEV_ANY_SUBCLASS;

        //tell kernel what we want to filter on
        ioctl(systemSocket, SIOCSKEVFILT, &kevRequest);

        //bytes received from system socket
        ssize_t bytesReceived = -1;

        //message from kext
        // ->size is cumulation of header, struct, and max length of a proc path
        char kextMsg[KEV_MSG_HEADER_SIZE + sizeof(struct AsusSMCMessage)] = {0};

        struct AsusSMCMessage *message = NULL;

        while (YES) {
            //printf("listening...\n");

            bytesReceived = recv(systemSocket, kextMsg, sizeof(kextMsg), 0);

            if (bytesReceived != sizeof(kextMsg)) continue;

            //struct for broadcast data from the kext
            struct kern_event_msg *kernEventMsg = {0};

            //type cast
            // ->to access kev_event_msg header
            kernEventMsg = (struct kern_event_msg*)kextMsg;

            //only care about 'process began' events
            if (AsusSMCEventCode != kernEventMsg->event_code) {
                //skip
                continue;
            }

            //printf("new message\n");

            //typecast custom data
            // ->begins right after header
            message = (struct AsusSMCMessage*)&kernEventMsg->event_data[0];

            printf("type:%d x:%d y:%d\n", message->type, message->x, message->y);

            switch (message->type) {
                case kevKeyboardBacklight:
                    showKBoardBLightStatus(message->x, message->y);
                    break;
                case kevAirplaneMode:
                    toggleAirplaneMode();
                    break;
                case kevSleep:
                    goToSleep();
                    break;
                default:
                    printf("unknown type %d\n", message->type);
            }
        }
    }

    return 0;
}
