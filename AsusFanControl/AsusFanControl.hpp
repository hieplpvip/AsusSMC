//
//  AsusFanControl.hpp
//  AsusFanControl
//
//  Fan telemetry and fan-mode control for ASUS laptops, split out of AsusSMC.
//
//  Why a separate kext: the fan data does not come from the EC or from the CPU.
//  It comes from the ASUS platform firmware, reached through the WMI/ATK ACPI
//  device - exactly the same channel AsusSMC uses for the keyboard backlight,
//  the ALS and the battery. Bundling both in one kext made the SMC layer
//  responsible for hardware it does not own, and made the fan code impossible
//  to use on its own.
//
//  The two kexts each match the ATKD ACPI device independently. ACPI devices
//  support several drivers, so matching it twice is allowed; what must not
//  happen twice is registering the same SMC key, which is why the keys moved
//  here wholesale rather than being shared.
//

#ifndef _AsusFanControl_hpp
#define _AsusFanControl_hpp

#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <VirtualSMCSDK/kern_vsmcapi.hpp>

#include "KeyImplementations.hpp"

// ATK / WMI identifiers. Only the ones this kext needs.
#define ASUS_WMI_METHODID_DSTS             0x53545344
#define ASUS_WMI_METHODID_DEVS             0x53564544
#define ASUS_WMI_METHODID_INIT             0x54494E49
#define ASUS_WMI_DEVID_CPU_FAN_CTRL        0x00110013
#define ASUS_WMI_DEVID_CPU_FAN2_CTRL       0x00110014
#define ASUS_WMI_DEVID_FAN_CTRL            0x00110012
#define ASUS_WMI_DEVID_FAN_BOOST_MODE      0x00110018
#define ASUS_WMI_DEVID_THROTTLE_THERMAL_POLICY 0x00120075
#define ASUS_WMI_MGMT_GUID                     "97845ED0-4E6D-11DE-8A39-0800200C9A66"

#define AsusFanControlEventCode 0x8102

// Build identification, same convention as AsusSMC: a boot that silently kept
// an older kext must be distinguishable from a failed experiment.
#define kAsusFanControlBuildTag "1.4.2"

// Fan control modes (DEVS values for the fan device).
enum {
    ASUS_FAN_CTRL_FULLSPEED = 0,   // exposed as F0Md = 0
    ASUS_FAN_CTRL_MANUAL    = 1,   // not offered: no per-fan PWM on this board
    ASUS_FAN_CTRL_AUTO      = 2,   // exposed as F0Md = 2, EC curve
};

// Thermal policy (DEVS 0x00120075). The DSDT If/ElseIf chain only covers 0/1/2;
// any other value leaves Local0 holding its previous contents and writes that to
// the EC, which on a GA503QM was measured to spin both fans to ~5000 RPM with no
// predictable result. Out-of-range values are rejected before they reach DEVS.
enum {
    ASUS_THERMAL_POLICY_BALANCED    = 0,
    ASUS_THERMAL_POLICY_PERFORMANCE = 1,
    ASUS_THERMAL_POLICY_SILENT      = 2,
};

// Plausible range published for the gauges in Macs Fan Control / iStat. The
// real limits live in the EC firmware and are not readable through DEVS/DSTS,
// so these are constants.
static constexpr uint16_t kFanMinRPM = 0;
static constexpr uint16_t kFanMaxRPM = 7300;

class AsusFanControl : public IOService {
    OSDeclareDefaultStructors(AsusFanControl)

    // Plugin descriptor, same shape as AsusSMC's. PRODUCT_NAME and
    // MODULE_VERSION come from the build's preprocessor definitions.
    VirtualSMCAPI::Plugin vsmcPlugin {
        xStringify(PRODUCT_NAME),
        parseModuleVersion(xStringify(MODULE_VERSION)),
        VirtualSMCAPI::Version,
    };

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

    // Registers with VirtualSMC and submits the plugin once the SMC service is
    // available, exactly as AsusSMC does.
    void registerVSMC();
    static bool vsmcNotificationHandler(void *sensors, void *refCon,
                                        IOService *vsmc, IONotifier *notifier);

    // --- called by the SMC key implementations -------------------------
    int  getFanSpeed(uint8_t index);
    int  setFanMode(uint8_t index, uint32_t mode);
    uint32_t getThermalProfile() const;
    int  setThermalProfile(uint32_t profile);
    bool isThermalProfileSupported() const { return isThermalPolicyPresent; }
    bool isFanDataValid() const {
        return atomic_load_explicit(&fanValid, memory_order_acquire) != 0;
    }

    // The SMC key values read these directly, so they are public. That is also
    // why they are atomics: the key reader runs on a different thread from the
    // refresh timer.
    _Atomic(uint16_t) fanSpeed0 = ATOMIC_VAR_INIT(0);
    _Atomic(uint16_t) fanSpeed1 = ATOMIC_VAR_INIT(0);
    _Atomic(uint32_t) fanMode0  = ATOMIC_VAR_INIT(ASUS_FAN_CTRL_AUTO);
    _Atomic(uint32_t) fanMode1  = ATOMIC_VAR_INIT(ASUS_FAN_CTRL_AUTO);
    _Atomic(uint32_t) thermalProfile = ATOMIC_VAR_INIT(ASUS_THERMAL_POLICY_BALANCED);

private:
    bool refreshAll();
    bool fanDevIsPresent(uint32_t dev_id);

    int  wmiEvaluateMethod(uint32_t methodId, uint32_t arg0, uint32_t arg1);

    void timerTick();
    static void timerAction(OSObject *owner, IOTimerEventSource *sender);

    IOACPIPlatformDevice *atkDevice {nullptr};
    IOCommandGate *commandGate {nullptr};
    IOTimerEventSource *timer {nullptr};
    OSSymbol *wmiMethod {nullptr};
    IONotifier *vsmcNotifier {nullptr};

    bool isFanPresent0 {false};
    bool isFanPresent1 {false};
    bool isFanCtrlPresent {false};
    bool isFanBoostPresent {false};
    bool isThermalPolicyPresent {false};
    bool probed {false};

    _Atomic(uint8_t) fanValid = ATOMIC_VAR_INIT(0);
};

#endif // _AsusFanControl_hpp
