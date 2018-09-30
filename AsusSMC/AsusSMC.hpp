//
//  AsusSMC.hpp
//  AsusSMC
//
//  Copyright © 2018 Le Bao Hiep. All rights reserved.
//

#ifndef _AsusSMC_hpp
#define _AsusSMC_hpp

#include <IOKit/hidsystem/ev_keymap.h>
#include <IOKit/pwr_mgt/IOPMPowerSource.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOService.h>
#include <IOKit/IONVRAM.h>
#include <IOKit/IOLib.h>
#include <sys/errno.h>
#include <mach/kern_return.h>
#include <sys/kern_control.h>
#include <libkern/OSTypes.h>

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
#define ASUS_WMI_MGMT_GUID      "97845ED0-4E6D-11DE-8A39-0800200C9A66"
#define ASUS_NB_WMI_EVENT_GUID  "0B3CBB35-E3C2-45ED-91C2-4C5A6D195D1C"

/* WMI Methods */
#define ASUS_WMI_METHODID_SPEC          0x43455053 /* BIOS SPECification */
#define ASUS_WMI_METHODID_SFBD          0x44424653 /* Set First Boot Device */
#define ASUS_WMI_METHODID_GLCD          0x44434C47 /* Get LCD status */
#define ASUS_WMI_METHODID_GPID          0x44495047 /* Get Panel ID?? (Resol) */
#define ASUS_WMI_METHODID_QMOD          0x444F4D51 /* Quiet MODe */
#define ASUS_WMI_METHODID_SPLV          0x4C425053 /* Set Panel Light Value */
#define ASUS_WMI_METHODID_SFUN          0x4E554653 /* FUNCtionalities */
#define ASUS_WMI_METHODID_SDSP          0x50534453 /* Set DiSPlay output */
#define ASUS_WMI_METHODID_GDSP          0x50534447 /* Get DiSPlay output */
#define ASUS_WMI_METHODID_DEVP          0x50564544 /* DEVice Policy */
#define ASUS_WMI_METHODID_OSVR          0x5256534F /* OS VeRsion */
#define ASUS_WMI_METHODID_DSTS          0x53544344 /* Device STatuS */
#define ASUS_WMI_METHODID_DSTS2         0x53545344 /* Device STatuS #2*/
#define ASUS_WMI_METHODID_BSTS          0x53545342 /* Bios STatuS ? */
#define ASUS_WMI_METHODID_DEVS          0x53564544 /* DEVice Set */
#define ASUS_WMI_METHODID_CFVS          0x53564643 /* CPU Frequency Volt Set */
#define ASUS_WMI_METHODID_KBFT          0x5446424B /* KeyBoard FilTer */
#define ASUS_WMI_METHODID_INIT          0x54494E49 /* INITialize */
#define ASUS_WMI_METHODID_HKEY          0x59454B48 /* Hot KEY ?? */

#define ASUS_WMI_UNSUPPORTED_METHOD     0xFFFFFFFE

/* Wireless */
#define ASUS_WMI_DEVID_HW_SWITCH        0x00010001
#define ASUS_WMI_DEVID_WIRELESS_LED     0x00010002
#define ASUS_WMI_DEVID_CWAP             0x00010003
#define ASUS_WMI_DEVID_WLAN             0x00010011
#define ASUS_WMI_DEVID_BLUETOOTH        0x00010013
#define ASUS_WMI_DEVID_GPS              0x00010015
#define ASUS_WMI_DEVID_WIMAX            0x00010017
#define ASUS_WMI_DEVID_WWAN3G           0x00010019
#define ASUS_WMI_DEVID_UWB              0x00010021

/* Leds */
/* 0x000200XX and 0x000400XX */
#define ASUS_WMI_DEVID_LED1             0x00020011
#define ASUS_WMI_DEVID_LED2             0x00020012
#define ASUS_WMI_DEVID_LED3             0x00020013
#define ASUS_WMI_DEVID_LED4             0x00020014
#define ASUS_WMI_DEVID_LED5             0x00020015
#define ASUS_WMI_DEVID_LED6             0x00020016

/* Backlight and Brightness */
#define ASUS_WMI_DEVID_BACKLIGHT        0x00050011
#define ASUS_WMI_DEVID_BRIGHTNESS       0x00050012
#define ASUS_WMI_DEVID_KBD_BACKLIGHT    0x00050021
#define ASUS_WMI_DEVID_LIGHT_SENSOR     0x00050022 /* ?? */

/* Misc */
#define ASUS_WMI_DEVID_CAMERA           0x00060013

/* Storage */
#define ASUS_WMI_DEVID_CARDREADER       0x00080013

/* Input */
#define ASUS_WMI_DEVID_TOUCHPAD         0x00100011
#define ASUS_WMI_DEVID_TOUCHPAD_LED     0x00100012

/* Fan, Thermal */
#define ASUS_WMI_DEVID_THERMAL_CTRL     0x00110011
#define ASUS_WMI_DEVID_FAN_CTRL         0x00110012

/* Power */
#define ASUS_WMI_DEVID_PROCESSOR_STATE  0x00120012

/* DSTS masks */
#define ASUS_WMI_DSTS_STATUS_BIT        0x00000001
#define ASUS_WMI_DSTS_UNKNOWN_BIT       0x00000002
#define ASUS_WMI_DSTS_PRESENCE_BIT      0x00010000
#define ASUS_WMI_DSTS_USER_BIT          0x00020000
#define ASUS_WMI_DSTS_BIOS_BIT          0x00040000
#define ASUS_WMI_DSTS_BRIGHTNESS_MASK   0x000000FF
#define ASUS_WMI_DSTS_MAX_BRIGTH_MASK   0x0000FF00


/*
 * <platform>/    - debugfs root directory
 *   dev_id      - current dev_id
 *   ctrl_param  - current ctrl_param
 *   method_id   - current method_id
 *   devs        - call DEVS(dev_id, ctrl_param) and print result
 *   dsts        - call DSTS(dev_id)  and print result
 *   call        - call method_id(dev_id, ctrl_param) and print result
 
 */

#define EEEPC_WMI_METHODID_SPEC 0x43455053
#define EEEPC_WMI_METHODID_DEVP 0x50564544
#define EEEPC_WMI_METHODID_DEVS    0x53564544
#define EEEPC_WMI_METHODID_DSTS    0x53544344
#define EEEPC_WMI_METHODID_CFVS    0x53564643

#define EEEPC_WMI_DEVID_BACKLIGHT    0x00050011
#define EEEPC_WMI_DEVID_BACKLIGHT2    0x00050012
#define EEEPC_WMI_DEVID_BLUETOOTH   0x00010013
#define EEEPC_WMI_DEVID_WIRELESS    0x00010011
#define EEEPC_WMI_DEVID_TRACKPAD    0x00100011

#define kIOPMPowerOff                       0
#define kAsusSMCIOPMNumberPowerStates     2
static IOPMPowerState powerStateArray[kAsusSMCIOPMNumberPowerStates] =
{
    { 1,kIOPMPowerOff,kIOPMPowerOff,kIOPMPowerOff,0,0,0,0,0,0,0,0 },
    { 1,kIOPMPowerOn,IOPMPowerOn,IOPMPowerOn,0,0,0,0,0,0,0,0 }
};

#define AsusSMCEventCode 0x8102

const UInt8 NOTIFY_BRIGHTNESS_UP_MIN = 0x10;
const UInt8 NOTIFY_BRIGHTNESS_UP_MAX = 0x1F;

const UInt8 NOTIFY_BRIGHTNESS_DOWN_MIN = 0x20;
const UInt8 NOTIFY_BRIGHTNESS_DOWN_MAX = 0x2F;

#define kDeliverNotifications "RM,deliverNotifications"
enum {
    kKeyboardSetTouchStatus = iokit_vendor_specific_msg(100),        // set disable/enable touchpad (data is bool*)
    kKeyboardGetTouchStatus = iokit_vendor_specific_msg(101),        // get disable/enable touchpad (data is bool*)
    kKeyboardKeyPressTime = iokit_vendor_specific_msg(110),          // notify of timestamp a non-modifier key was pressed (data is uint64_t*)
};

enum {
    kevKeyboardBacklight = 1,
    kevAirplaneMode = 2,
    kevSleep = 3,
    kevTouchpad = 4,
};

enum ReportType {
    none = 0,
    keyboard_input = 1,
    consumer_input = 2,
    apple_vendor_top_case_input = 3,
    apple_vendor_keyboard_input = 4,
};

typedef struct  {
    UInt16 in;
    UInt8 out;
    ReportType type;
} FnKeysKeyMap;

class EXPORT AsusSMC : public IOService {
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
    virtual IOReturn message(UInt32 type, IOService * provider, void * argument) override;

    // standard IOKit methods
    virtual bool init(OSDictionary *dictionary = 0) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual IOService *probe(IOService *provider, SInt32 *score) override;
    
    //power management events
    virtual IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker) override;

protected:

    /**
     *  Asus ATK device
     */
    IOACPIPlatformDevice * atkDevice;

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
    IOCommandGate* command_gate;

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
    VirtualHIDKeyboard *_virtualKBrd;

    karabiner_virtual_hid_device::hid_report::keyboard_input kbreport;
    karabiner_virtual_hid_device::hid_report::apple_vendor_top_case_input tcreport;

    /**
     *  Map Fn key events to Apple keyboard codes
     */
    static const FnKeysKeyMap keyMap[];

    /**
     *  Touchpad enabled status
     */
    bool touchpadEnabled {true};

    /**
     *  Keyboard backlight availability
     */
    bool hasKeybrdBLight {false};

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

    OSDictionary * properties;

    OSDictionary* getDictByUUID(const char * guid);

    /**
     *  Initialize virtual HID keyboard
     */
    void initVirtualKeyboard();

    /**
     *  Check ALS and keyboard backlight availability
     */
    void checkKBALS();

    /**
     *  Handle message from ATK
     */
    void handleMessage(int code);

    /**
     *  Simulate keyboard events, taken from Karabiner-Elements
     */
    IOReturn postKeyboardInputReport(const void* report, uint32_t reportSize);

    /**
     *  Process keyboard events
     */
    void processFnKeyEvents(int code, int bLoopCount);

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

    void getDeviceStatus(const char * guid, UInt32 methodId, UInt32 deviceId, UInt32 *status);
    void setDeviceStatus(const char * guid, UInt32 methodId, UInt32 deviceId, UInt32 *status);
    void setDevice(const char * guid, UInt32 methodId, UInt32 *status);

    /**
     *  Send notifications to 3rd-party drivers (eg. VoodooI2C)
     */
    IONotifier* _publishNotify;
    IONotifier* _terminateNotify;
    OSSet* _notificationServices;
    void registerNotifications(void);
    void notificationHandlerGated(IOService * newService, IONotifier * notifier);
    bool notificationHandler(void * refCon, IOService * newService, IONotifier * notifier);
    void dispatchMessageGated(int* message, void* data);
    void dispatchMessage(int message, void* data);

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
     * wmi_parse_hexbyte - Convert a ASCII hex number to a byte
     * @param src  Pointer to at least 2 characters to convert.
     *
     * Convert a two character ASCII hex string to a number.
     *
     * @return  0-255  Success, the byte was parsed correctly
     *          -1     Error, an invalid character was supplied
     */
    int wmi_parse_hexbyte(const UInt8 *src);

    /**
     * wmi_swap_bytes - Rearrange GUID bytes to match GUID binary
     * @param src   Memory block holding binary GUID (16 bytes)
     * @param dest  Memory block to hold byte swapped binary GUID (16 bytes)
     *
     * Byte swap a binary GUID to match it's real GUID value
     */
    void wmi_swap_bytes(UInt8 *src, UInt8 *dest);

    /**
     * wmi_parse_guid - Convert GUID from ASCII to binary
     * @param src   36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
     * @param dest  Memory block to hold binary GUID (16 bytes)
     *
     * N.B. The GUID need not be NULL terminated.
     *
     * @return  'true'   @dest contains binary GUID
     *          'false'  @dest contents are undefined
     */
    bool wmi_parse_guid(const UInt8 *src, UInt8 *dest);

    /**
     * wmi_dump_wdg - dumps tables to dmesg
     * @param src guid_block *
     */
    void wmi_dump_wdg(struct guid_block *src);

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

    /**
     *  Parse the _WDG method for the GUID data blocks
     */
    int parse_wdg(OSDictionary *dict);

    OSDictionary * readDataBlock(char *str);
};

#endif //_AsusSMC_hpp
