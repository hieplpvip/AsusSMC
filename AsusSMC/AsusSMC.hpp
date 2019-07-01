//
//  AsusSMC.hpp
//  AsusSMC
//
//  Copyright © 2018-2019 Le Bao Hiep. All rights reserved.
//

#ifndef _AsusSMC_hpp
#define _AsusSMC_hpp

#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOCommandGate.h>
#include "karabiner_virtual_hid_device.hpp"
#include "VirtualHIDKeyboard.hpp"
#include "KernEventServer.hpp"
#include "KeyImplementations.hpp"

struct guid_block {
    char guid[16];
    union {
        char object_id[2];
        struct {
            unsigned char notify_id;
            unsigned char reserved;
        };
    };
    UInt8 instance_count;
    UInt8 flags;
};

/*
 * If the GUID data block is marked as expensive, we must enable and
 * explicitily disable data collection.
 */
#define ACPI_WMI_EXPENSIVE   0x1
#define ACPI_WMI_METHOD      0x2    /* GUID is a method */
#define ACPI_WMI_STRING      0x4    /* GUID takes & returns a string */
#define ACPI_WMI_EVENT       0x8    /* GUID is an event */

#define AsusSMCEventCode 0x8102

const UInt8 NOTIFY_BRIGHTNESS_UP_MIN = 0x10;
const UInt8 NOTIFY_BRIGHTNESS_UP_MAX = 0x1F;

const UInt8 NOTIFY_BRIGHTNESS_DOWN_MIN = 0x20;
const UInt8 NOTIFY_BRIGHTNESS_DOWN_MAX = 0x2F;

#define kDeliverNotifications "RM,deliverNotifications"
enum {
    kKeyboardSetTouchStatus = iokit_vendor_specific_msg(100), // set disable/enable touchpad (data is bool*)
    kKeyboardGetTouchStatus = iokit_vendor_specific_msg(101), // get disable/enable touchpad (data is bool*)
    kKeyboardKeyPressTime = iokit_vendor_specific_msg(110),   // notify of timestamp a non-modifier key was pressed (data is uint64_t*)
};

enum {
    kevKeyboardBacklight = 1,
    kevAirplaneMode = 2,
    kevSleep = 3,
    kevTouchpad = 4,
};

class AsusSMC : public IOService {
    OSDeclareDefaultStructors(AsusSMC)

    /**
     *  Registered plugin instance
     */
    VirtualSMCAPI::Plugin vsmcPlugin {
        xStringify(PRODUCT_NAME),
        parseModuleVersion(xStringify(MODULE_VERSION)),
        VirtualSMCAPI::Version,
    };

public:
    virtual IOReturn message(UInt32 type, IOService *provider, void *argument) override;

    virtual bool init(OSDictionary *dictionary = 0) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual IOService *probe(IOService *provider, SInt32 *score) override;

    void letSleep();
    void toggleAirplaneMode();
    void toggleTouchpad();
    void displayOff();
protected:

    OSDictionary *properties {nullptr};

    /**
     *  Asus ATK device
     */
    IOACPIPlatformDevice *atkDevice {nullptr};

    /**
     *  Current lux value obtained from ACPI
     */
    _Atomic(uint32_t) currentLux;

    /**
     *  Supported ALS bits
     */
    ALSForceBits forceBits;

    /**
     *  VirtualSMC service registration notifier
     */
    IONotifier *vsmcNotifier {nullptr};

    /**
     *  A workloop in charge of handling timer events with requests.
     */
    IOWorkLoop *workloop {nullptr};

    /**
     *  Executes an action on the driver's work-loop
     */
    IOCommandGate *command_gate {nullptr};

    /**
     *  Workloop timer event source for status updates
     */
    IOTimerEventSource *poller {nullptr};

    /**
     *  Interrupt submission timeout
     */
    static constexpr uint32_t SensorUpdateTimeoutMS {1000};

    /**
     *  Send commands to user-space daemon
     */
    KernEventServer kev;

    /**
     *  Virtual keyboard device
     */
    VirtualHIDKeyboard *_virtualKBrd {nullptr};

    karabiner_virtual_hid_device::hid_report::consumer_input csmrreport;
    karabiner_virtual_hid_device::hid_report::apple_vendor_top_case_input tcreport;

    /**
     *  Touchpad enabled status
     */
    bool touchpadEnabled {true};

    /**
     *  Keyboard backlight availability
     */
    bool hasKeybrdBLight {false};

    /**
     *  Direct ACPI messaging support
     *  Originally, receiving ACPI messages takes several unnecessary steps (thanks, ASUS!)
     *  By patching method IANE in DSDT, we can avoid those steps
     */
    bool directACPImessaging {false};

    /**
     *  ALS availability
     */
    bool hasALSensor {false};

    /**
     *  ALS enabled status
     */
    bool isALSenabled {false};

    /**
     *  Backlight status (Fn+F7)
     */
    bool isPanelBackLightOn {true};

    /**
     *  Handle message from ATK
     */
    void handleMessage(int code);

    /**
     *  Check ALS and keyboard backlight availability
     */
    void checkATK();

    /**
     *  Enable/Disable ALS sensor
     */
    void toggleALS(bool state);

    /**
     *  Brightness
     */
    UInt32 panelBrightnessLevel {16};
    char backlightEntry[1000];
    int checkBacklightEntry();
    int findBacklightEntry();

    /**
     *  Reading AppleBezel Values from Apple Backlight Panel driver for controlling the bezel levels
     */
    void readPanelBrightnessValue();

    /**
     *  Initialize virtual HID keyboard
     */
    void initVirtualKeyboard();

    /**
     *  Simulate keyboard events, taken from Karabiner-Elements
     */
    IOReturn postKeyboardInputReport(const void *report, uint32_t reportSize);

    void dispatchCSMRReport(int code, int loop = 1);
    void dispatchTCReport(int code, int loop = 1);

    /**
     *  Send notifications to 3rd-party drivers (eg. VoodooI2C)
     */
    IONotifier *_publishNotify {nullptr};
    IONotifier *_terminateNotify {nullptr};
    OSSet *_notificationServices {nullptr};
    void registerNotifications(void);
    void notificationHandlerGated(IOService *newService, IONotifier *notifier);
    bool notificationHandler(void *refCon, IOService *newService, IONotifier *notifier);
    void dispatchMessageGated(int *message, void *data);
    void dispatchMessage(int message, void* data);

    /**
     *  HID drivers
     */
    OSSet *_hidDrivers {nullptr};

    /**
     *  Register ourself as a VirtualSMC plugin
     */
    void registerVSMC(void);

    /**
     *  Submit the keys to received VirtualSMC service.
     *
     *  @param sensors   AsusSMC service
     *  @param refCon    reference
     *  @param vsmc      VirtualSMC service
     *  @param notifier  created notifier
     */
    static bool vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier);

    /**
     *  Refresh sensor values to inform macOS with light changes
     *
     *  @param post  post an SMC notification
     */
    bool refreshSensor(bool post);

private:
    /**
     * wmi_data2Str - converts binary guid to ascii guid
     *
     */
    int wmi_data2Str(const char *in, char *out);

    /**
     * flagsToStr - converts binary flag to ascii flag
     *
     */
    OSString *flagsToStr(UInt8 flags);

    /**
     * wmi_wdg2reg - adds the WDG structure to a dictionary
     *
     */
    void wmi_wdg2reg(struct guid_block *g, OSArray *array, OSArray *dataArray);

    OSDictionary * readDataBlock(char *str);

    /**
     *  Parse the _WDG method for the GUID data blocks
     */
    int parse_wdg(OSDictionary *dict);

    OSDictionary *getDictByUUID(const char *guid);
};

#endif //_AsusSMC_hpp
