//
//  AsusSMC.hpp
//  AsusSMC
//
//  Copyright © 2018-2020 Le Bao Hiep. All rights reserved.
//

#ifndef _AsusSMC_hpp
#define _AsusSMC_hpp

#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include "HIDReport.hpp"
#include "HIDUsageTables.h"
#include "VirtualAppleKeyboard.hpp"
#include "KernEventServer.hpp"
#include "KeyImplementations.hpp"

#define ASUS_WMI_METHODID_DSTS         0x53545344
#define ASUS_WMI_METHODID_DEVS         0x53564544
#define ASUS_WMI_METHODID_INIT         0x54494E49
#define ASUS_WMI_DEVID_ALS_ENABLE      0x00050001
#define ASUS_WMI_DEVID_CPU_FAN_CTRL    0x00110013
#define ASUS_WMI_DEVID_CPU_FAN2_CTRL   0x00110014
#define ASUS_WMI_DEVID_FAN_CTRL        0x00110012
#define ASUS_WMI_DEVID_FAN_BOOST_MODE  0x00110018
#define ASUS_WMI_DEVID_THROTTLE_THERMAL_POLICY 0x00120075
#define ASUS_WMI_DEVID_RSOC            0x00120057
#define ASUS_WMI_DEVID_KEYBOARD_BACKLIGHT 0x00050021
// Keyboard backlight protocol, same as Linux include/linux/platform_data/
// x86/asus-wmi.h: the firmware has ASUS_EV_MAX_BRIGHTNESS 3, i.e. four levels
// 0..3, and bit 7 of the control word means "light on".
#define ASUS_KBD_BACKLIGHT_MAX      3
#define ASUS_KBD_BACKLIGHT_ON_BIT   0x80
// USB vendor id of the keyboard, needed to find its IOHIDDevice.
#define ASUS_USB_VENDOR_ID          0x0B05
#define ASUS_WMI_DSTS_PRESENCE_BIT     0x00010000
#define ASUS_WMI_UNSUPPORTED_METHOD    0xFFFFFFFE

// Fan control modes and thermal policies moved to AsusFanControl.kext together
// with the fan code and the SMC keys they describe.

#define ASUS_WMI_MGMT_GUID             "97845ED0-4E6D-11DE-8A39-0800200C9A66"

#define kDeliverNotifications "RM,deliverNotifications"

#define AsusSMCEventCode 0x8102

// Build identification.
//
// Diagnostics that depend on knowing exactly which binary is running: a boot
// that silently kept an older kext is indistinguishable from a failed
// experiment, and two rounds were lost that way. Every build now stamps a tag
// into the registry as AsusSMCBuild.
//
// Update this string whenever behaviour changes.
#define kAsusSMCBuildTag "r45-injtest"


// Kernel HID device, used for the keyboard backlight feature report. Only a
// pointer is stored here, so a forward declaration is enough.
class IOHIDDevice;

class AsusSMC : public IOService {
    OSDeclareDefaultStructors(AsusSMC)

    VirtualSMCAPI::Plugin vsmcPlugin {
        xStringify(PRODUCT_NAME),
        parseModuleVersion(xStringify(MODULE_VERSION)),
        VirtualSMCAPI::Version,
    };

public:
    bool init(OSDictionary *dictionary) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    IOReturn message(uint32_t type, IOService *provider, void *argument) override;

    void letSleep();
    void toggleAirplaneMode();
    void toggleTouchpad();
    void toggleALS(bool state);
    void toggleBatteryConservativeMode(bool state);
    void displayOff();

    // Fan telemetry and fan-mode control live in AsusFanControl.kext, which
    // matches the ATKD device in its own right. The SMC keys moved with them
    // because a key can only be registered once.

    // Keyboard backlight over USB HID.
    //
    // The ACPI route for the backlight is a dead end on this board: the DSDT's
    // EC0.SLKB computes a level into a local and never uses it, so writing
    // DEVS 0x00050021 changes nothing. The firmware only reacts to a HID
    // feature report:
    //
    //   report id 0x5A, first bytes 5A BA C5 C4 <level>
    //
    // IOHIDDevice::setReport is a kernel API, so the kext can drive it
    // directly and macOS's own keyboard-brightness control then works. These
    // are public because SMCKBrdBLightValue calls them.
    bool ensureKbdHIDDevice();
    int  sendKbdBacklightViaHID(uint8_t level);

    uint32_t getLastKbdKey() const { return lastKbdKey; }
    uint32_t getKbdKeyCount() const { return kbdKeyCount; }

    // Diagnostic sink for the raw LKSB payload. Called from the SMC key
    // implementation, which cannot write IORegistry properties itself.
    void recordKbdWrite(uint8_t val1, uint8_t val2, uint16_t packed, uint16_t level);

    // Keyboard backlight following the display power state. macOS does not do
    // this on this model (see the comment on the implementation), so the
    // driver blanks and restores the keyboard itself.
    void followDisplayPower();
    bool findPanelEntryBySearch();
    // IODisplayWrangler power state: GPU independent, so it is the primary
    // signal for "is the display asleep".
    bool readDisplayWranglerState(uint32_t *state);
    // Walk descendants looking for a power management dictionary.
    bool trySubtree(IOService *root, int depth, uint32_t *state, const char **source);
    // One-shot report of every hop in the display power chain, published as
    // AsusSMCPowerDiag so a broken link is measured rather than guessed at.
    void probePowerRegistry();
    // Set after probePowerRegistry() has run once.
    bool powerProbeDone {false};
    // Display power notifications. The registry cannot be polled for this, so
    // the display power manager is asked to send its transitions instead.
    void subscribeDisplayPower();
    void publishDisplayEvents();
    uint32_t displayOffEvents {0};
    uint32_t displayOnEvents {0};
    // Both of these must be retried until the display services resolve: on the
    // first timer tick they do not exist yet, and a one-shot flag set there
    // silently disabled the feature forever.
    bool displayInterestDone {false};
    uint32_t pollTicks {0};
    // Short history of the raw LKSB payloads, so a repeated zero can be told
    // apart from a sequence.
    char kbdWriteLog[128] {};
    size_t kbdWriteLogPos {0};
    // System sleep/wake handling, ported from upstream PR #62.
    void subscribePowerEvents(IOService *provider);
    IOReturn setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) override;
    bool pmJoined {false};
    // Keyboard brightness writes arriving through the virtual Apple keyboard.
    static void virtualKeyboardBacklightSink(const uint8_t *bytes, uint32_t length, void *context);
    void handleVirtualKeyboardBacklight(const uint8_t *bytes, uint32_t length);
    // While the display is asleep the driver owns the backlight: any macOS
    // request (SMC LKSB or the virtual keyboard) is recorded but not applied,
    // so the auto-brightness loop cannot relight the keyboard we just blanked.
    bool backlightLockedWhileDimmed {false};
    uint32_t lockedRejectCount {0};

    // Follow the brightness value macOS publishes for the keyboard backlight.
    //
    // Measured on the machine: the registry holds KeyboardBacklightBrightness
    // and it tracks the System Settings slider exactly (0, 689, 1696, 3838,
    // 4091 - the full 0..0xFFB range). macOS never forwards it to the hardware
    // on this keyboard, so this driver reads it and drives the light itself.
    void followKeyboardBrightness();
    void findBrightnessEntryByProperty();
    bool readBrightnessValue(uint32_t *value);
    IORegistryEntry *blEntry {nullptr};
    uint32_t blValue {0};
    uint32_t blWrites {0};
    bool blFoundPublished {false};
    // Set from -asussmcnofollow in start(). A function-local static cannot be
    // used here: the kernel has no guard variable support.
    bool blFollowDisabled {false};

    // Read/write counters for LKSB (key 0) and LKSS (key 1). macOS only
    // writes LKSB when the keyboard brightness control is actually used, so
    // these numbers separate "the control is not wired up" from "the write
    // was rejected".
    void recordLKSAccess(uint32_t key, bool isWrite, uint16_t value);
    void publishLKSAccess();

private:
    struct guid_block {
        char guid[16];
        union {
            char object_id[2];
            struct {
                uint8_t notify_id;
                uint8_t reserved;
            };
        };
        uint8_t instance_count;
        uint8_t flags;
    };

    // Same layout as struct bios_args in Linux asus-wmi.c (24 bytes).
    // Some ATK DSDTs (e.g. GA503QM) do CreateDWordField (Arg2, 0x10, IIA4) in
    // WMNB, so the buffer must be at least 0x14 bytes or the method aborts.
    struct wmi_args {
        uint32_t arg0;
        uint32_t arg1;
        uint32_t arg2;
        uint32_t arg3;
        uint32_t arg4;
        uint32_t arg5;
    } __packed;

    enum {
        kKeyboardSetTouchStatus = iokit_vendor_specific_msg(100), // set disable/enable touchpad (data is bool*)
        kKeyboardGetTouchStatus = iokit_vendor_specific_msg(101), // get disable/enable touchpad (data is bool*)
        kKeyboardKeyPressTime = iokit_vendor_specific_msg(110),   // notify of timestamp a non-modifier key was pressed (data is uint64_t*)
    };

    enum {
        kDaemonKeyboardBacklight = 1,
        kDaemonAirplaneMode = 2,
        kDaemonSleep = 3,
        kDaemonTouchpad = 4,
    };

    static constexpr uint32_t SensorUpdateTimeoutMS {1000};

    static constexpr uint8_t NOTIFY_BRIGHTNESS_UP_MIN = 0x10;
    static constexpr uint8_t NOTIFY_BRIGHTNESS_UP_MAX = 0x1F;

    static constexpr uint8_t NOTIFY_BRIGHTNESS_DOWN_MIN = 0x20;
    static constexpr uint8_t NOTIFY_BRIGHTNESS_DOWN_MAX = 0x2F;

    char wmi_method[5];
    bool wmiMethodFound {false};
    int wmi_parse_guid(const char *in, char *out);
    int wmi_evaluate_method(uint32_t method_id, uint32_t arg0, uint32_t arg1);
    int wmi_get_devstate(uint32_t dev_id);
    bool wmi_dev_is_present(uint32_t dev_id);
    // Presence check for devices that only exist as a DEVS branch and have
    // no DSTS (the generic 0x00050021 keyboard backlight path).
    bool pci0Ec0HasKeyboardBacklight();
    void parse_WDG();

    void initATKDevice();
    void probeATKEventPath();
    void initALSDevice();
    void initEC0Device();
    void initBattery();
    void initVirtualKeyboard();

    void startATKDevice();

    bool refreshALS(bool post);

    void handleMessage(int code);

    IOACPIPlatformDevice *atkDevice {nullptr};
    IOACPIPlatformDevice *alsDevice {nullptr};
    IOACPIPlatformDevice *ec0Device {nullptr};
    VirtualAppleKeyboard *kbdDevice {nullptr};

    ALSForceBits forceBits;
    _Atomic(uint32_t) currentLux = ATOMIC_VAR_INIT(0);

    IONotifier *vsmcNotifier {nullptr};

    IOWorkLoop *workloop {nullptr};
    IOCommandGate *command_gate {nullptr};
    IOTimerEventSource *poller {nullptr};

    KernEventServer kev;

    consumer_input csmrreport;
    apple_vendor_top_case_input tcreport;

    bool directACPImessaging {false};
    bool hasKeyboardBacklight {false};
    // Some ATK DSDTs (GA503QM included) expose no SKBV method at all and route
    // the keyboard backlight through the generic WMI device 0x00050021 ->
    // EC0.SLKB instead. That path only exists as a DEVS branch, with no DSTS,
    // so the level can be written but never read back.
    bool keyboardBacklightViaWMI {false};
    // The ASUS vendor HID interface, used to write the backlight feature
    // report. Resolved lazily because the keyboard is enumerated by USB well
    // after the ACPI device this driver matches on.
    IOHIDDevice *kbdHIDDevice {nullptr};
    // Diagnostics: the EC raises many notifications and we need to know which
    // ones actually arrive on macOS. Read back with
    //   ioreg -rn AsusSMC -l | grep AsusSMCKey
    uint32_t lastKbdKey {0};
    uint32_t kbdKeyCount {0};
    uint32_t notifyCount {0};
    uint32_t injectTicks {0};
    uint32_t injectRet {0};
    bool injected {false};
    // Number of LKSB writes seen, so a stalled brightness control is obvious.
    uint32_t kbdWriteCount {0};
    // Per-key access counters: LKSB and LKSS, reads and writes.
    uint32_t lksReads[2] {0, 0};
    uint32_t lksWrites[2] {0, 0};
    uint16_t lksLastValue {0};
    // Keyboard backlight level to restore after the panel comes back, and
    // whether we are the ones who blanked it.
    uint8_t kbdLevel {2};
    bool kbdDimmedForDisplay {false};
    bool panelUnreadable {false};
    // The boot-time write fails because the USB keyboard is not up yet, so it
    // is retried from the sensor timer until it succeeds.
    bool kbdBootApplied {false};
    // Consecutive polls that looked like "display off"; two are required
    // before blanking, so a single transient value cannot do it.
    uint32_t offStreak {0};
    // Running record of every value the display signals took. The display
    // cannot be probed from a shell while it is asleep, so the driver keeps
    // the history and it is read back afterwards.
    bool traceSeen {false};
    int traceWranglerMin {0};
    int traceWranglerMax {0};
    int traceBrightnessMin {-1};
    int traceBrightnessMax {-1};
    // Cached display service that publishes IODisplayParameters.brightness.
    IORegistryEntry *panelEntry {nullptr};
    // Cached IODisplayWrangler, resolved by class name.
    IOService *wranglerEntry {nullptr};
    // Whichever entry actually answered with a power state, cached so the
    // search cost is paid once.
    IOService *powerEntry {nullptr};
    bool isALSEnabled {true};
    bool isTouchpadEnabled {true};
    bool isPanelBackLightOn {true};
    bool isBatteryRSOCAvailable {false};

    uint32_t panelBrightnessLevel {16};
    char backlightEntry[1000];

    int checkBacklightEntry();
    int findBacklightEntry();
    // Returns false when the panel brightness could not be read at all, which
    // is the normal case on an AMD GPU whose service is not at the hard-coded
    // Intel/NVIDIA paths.
    bool readPanelBrightnessValue();

    IOReturn postKeyboardInputReport(const void *report, uint32_t reportSize);
    void dispatchCSMRReport(int code, int loop = 1);
    void dispatchTCReport(int code, int loop = 1);

    IONotifier *_publishNotify {nullptr};
    IONotifier *_terminateNotify {nullptr};
    OSSet *_notificationServices {nullptr};
    void registerNotifications(void);
    void notificationHandlerGated(IOService *newService, IONotifier *notifier);
    bool notificationHandler(void *refCon, IOService *newService, IONotifier *notifier);
    void dispatchMessageGated(int *message, void *data);
    void dispatchMessage(int message, void *data);

    void registerVSMC(void);
    static bool vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier);
};

#endif //_AsusSMC_hpp
