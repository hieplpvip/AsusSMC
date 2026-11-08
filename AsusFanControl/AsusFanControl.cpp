//
//  AsusFanControl.cpp
//  AsusFanControl
//
//  Fan telemetry and fan-mode control for ASUS laptops.
//
//  Split out of AsusSMC. The fan data comes from the ASUS platform firmware via
//  the WMI/ATK ACPI device, reached with DSVS/DSTS method calls. This kext
//  matches that device itself, so it can be installed without AsusSMC and stays
//  usable on machines where the keyboard backlight is not wanted.
//

#include "AsusFanControl.hpp"

#define super IOService
OSDefineMetaClassAndStructors(AsusFanControl, super)

// Lilu's logging macros expect these symbols, which plugin_start.cpp normally
// defines. It is not compiled here (see the CI comments), so they are defined
// directly.
bool ADDPR(debugEnabled) = false;
uint32_t ADDPR(debugPrintDelay) = 0;

// One-shot VirtualSMC plugin registration.
static AsusFanControl *gFanControl = nullptr;

IOService *AsusFanControl::probe(IOService *provider, SInt32 *score) {
    IOService *result = super::probe(provider, score);
    if (!result) {
        return nullptr;
    }

    IOACPIPlatformDevice *dev = OSDynamicCast(IOACPIPlatformDevice, provider);
    if (!dev) {
        return nullptr;
    }

    // The ATK device is identified by the methods it implements. Requiring the
    // WMI entry point keeps this kext away from unrelated ACPI devices that
    // happen to have a similar name.
    if (dev->validateObject("WMNB") != kIOReturnSuccess) {
        DBGLOG("fan", "provider has no WMNB, not the ATK device");
        return nullptr;
    }

    return result;
}

bool AsusFanControl::start(IOService *provider) {
    if (!super::start(provider)) {
        return false;
    }

    atkDevice = OSDynamicCast(IOACPIPlatformDevice, provider);
    if (!atkDevice) {
        SYSLOG("fan", "provider is not an IOACPIPlatformDevice");
        return false;
    }

    setProperty("AsusFanControlBuild", kAsusFanControlBuildTag);

    wmiMethod = const_cast<OSSymbol *>(OSSymbol::withCStringNoCopy("WMNB"));
    if (!wmiMethod) {
        SYSLOG("fan", "could not allocate the WMNB symbol");
        return false;
    }

    // Tell the firmware a driver is here, exactly as AsusSMC does. Without
    // this the ATK device answers nothing useful.
    wmiEvaluateMethod(ASUS_WMI_METHODID_INIT, 0, 0);

    commandGate = IOCommandGate::commandGate(this);
    IOWorkLoop *workloop = getWorkLoop();
    if (!commandGate || !workloop || workloop->addEventSource(commandGate) != kIOReturnSuccess) {
        SYSLOG("fan", "could not set up the work loop / command gate");
        return false;
    }

    // Probe once, up front, so the SMC keys can report the right shapes.
    // refreshAll() does the probing on its first call.
    refreshAll();

    char probe[96];
    snprintf(probe, sizeof(probe), "fan0=%d fan1=%d fanCtrl=%d boost=%d thermal=%d",
             isFanPresent0 ? 1 : 0, isFanPresent1 ? 1 : 0,
             isFanCtrlPresent ? 1 : 0, isFanBoostPresent ? 1 : 0,
             isThermalPolicyPresent ? 1 : 0);
    setProperty("AsusFanControlProbe", probe);

    // Publish the SMC keys now that there is a live instance for them to use.
    // Registration happens here rather than in plugin_start.cpp: Lilu's plugin
    // entry points are supplied by libkmod.a, and including plugin_start.cpp
    // standalone drags in the disassembler headers, which do not build in this
    // kernel configuration. AsusSMC's Xcode target does not compile it either.
    gFanControl = this;
    registerVSMC();

    timer = IOTimerEventSource::timerEventSource(this, &AsusFanControl::timerAction);
    if (!timer || workloop->addEventSource(timer) != kIOReturnSuccess) {
        SYSLOG("fan", "could not create the refresh timer");
        return false;
    }
    timer->setTimeoutMS(2000);

    registerService();

    return true;
}

void AsusFanControl::stop(IOService *provider) {
    if (timer) {
        timer->cancelTimeout();
        if (IOWorkLoop *wl = getWorkLoop()) {
            wl->removeEventSource(timer);
        }
        OSSafeReleaseNULL(timer);
    }
    if (commandGate) {
        if (IOWorkLoop *wl = getWorkLoop()) {
            wl->removeEventSource(commandGate);
        }
        OSSafeReleaseNULL(commandGate);
    }
    // wmiMethod came from withCStringNoCopy, which returns a cached symbol that
    // must not be released.
    wmiMethod = nullptr;
    gFanControl = nullptr;

    super::stop(provider);
}

void AsusFanControl::timerAction(OSObject *owner, IOTimerEventSource *sender) {
    AsusFanControl *self = OSDynamicCast(AsusFanControl, owner);
    if (self) {
        self->timerTick();
    }
    if (sender) {
        sender->setTimeoutMS(2000);
    }
}

void AsusFanControl::timerTick() {
    refreshAll();

    char rec[96];
    snprintf(rec, sizeof(rec), "fan0=%u fan1=%u valid=%d",
             (unsigned)atomic_load_explicit(&fanSpeed0, memory_order_acquire),
             (unsigned)atomic_load_explicit(&fanSpeed1, memory_order_acquire),
             isFanDataValid() ? 1 : 0);
    setProperty("AsusFanControlRPM", rec);
}

int AsusFanControl::wmiEvaluateMethod(uint32_t methodId, uint32_t arg0, uint32_t arg1) {
    if (!atkDevice || !wmiMethod) {
        return -1;
    }

    // The ACPI method expects a 20 byte argument buffer (method id + two
    // uint32 arguments + padding to the DSDT's declared size). Sending fewer
    // bytes makes the interpreter reject the call with AE_AML_BUFFER_LIMIT,
    // which is what upstream's 8 byte buffer did on this machine.
    struct {
        uint32_t methodId;
        uint32_t arg0;
        uint32_t arg1;
        uint8_t  pad[8];
    } args = {};

    args.methodId = methodId;
    args.arg0 = arg0;
    args.arg1 = arg1;

    OSObject *params[2];
    params[0] = OSNumber::withNumber(static_cast<uint32_t>(0), 32);
    params[1] = OSData::withBytes(&args, sizeof(args));

    uint32_t val = 0;
    IOReturn ret = atkDevice->evaluateInteger(wmiMethod, &val, params, 2);
    if (params[0]) params[0]->release();
    if (params[1]) params[1]->release();

    if (ret != kIOReturnSuccess) {
        return -1;
    }
    if (val == 0xfffffffe) {
        return -1;
    }
    return (int)val;
}

bool AsusFanControl::fanDevIsPresent(uint32_t dev_id) {
    int status = wmiEvaluateMethod(ASUS_WMI_METHODID_DSTS, dev_id, 0);
    if (status == -1) {
        return false;
    }
    return (status & 0x00010000) != 0;
}

int AsusFanControl::getFanSpeed(uint8_t index) {
    uint32_t dev_id = index ? ASUS_WMI_DEVID_CPU_FAN2_CTRL : ASUS_WMI_DEVID_CPU_FAN_CTRL;

    // Same decoding asus-wmi.c uses for fan1_input: the low half is the fan
    // counter in units of 100 RPM, the high half only carries flags.
    int status = wmiEvaluateMethod(ASUS_WMI_METHODID_DSTS, dev_id, 0);
    if (status == -1) {
        return -1;
    }
    if ((status & 0x00010000) == 0) {
        return -1;
    }

    return (int)((status & 0xffff) * 100);
}

bool AsusFanControl::refreshAll() {
    if (!probed) {
        probed = true;

        isFanPresent0 = fanDevIsPresent(ASUS_WMI_DEVID_CPU_FAN_CTRL);
        isFanPresent1 = fanDevIsPresent(ASUS_WMI_DEVID_CPU_FAN2_CTRL);
        isFanCtrlPresent = fanDevIsPresent(ASUS_WMI_DEVID_FAN_CTRL);
        isFanBoostPresent = fanDevIsPresent(ASUS_WMI_DEVID_FAN_BOOST_MODE);
        isThermalPolicyPresent = fanDevIsPresent(ASUS_WMI_DEVID_THROTTLE_THERMAL_POLICY);

        DBGLOG("fan", "probe: fan0 %d fan1 %d fan_ctrl %d boost %d thermal %d",
               isFanPresent0, isFanPresent1, isFanCtrlPresent, isFanBoostPresent,
               isThermalPolicyPresent);
    }

    int s0 = isFanPresent0 ? getFanSpeed(0) : -1;
    int s1 = isFanPresent1 ? getFanSpeed(1) : -1;

    if (s0 < 0 && s1 < 0) {
        atomic_store_explicit(&fanValid, 0, memory_order_release);
        return false;
    }

    // A single failed read is not enough to declare the data invalid.
    if (s0 >= 0) {
        atomic_store_explicit(&fanSpeed0, (uint16_t)s0, memory_order_release);
    }
    if (s1 >= 0) {
        atomic_store_explicit(&fanSpeed1, (uint16_t)s1, memory_order_release);
    }
    atomic_store_explicit(&fanValid, 1, memory_order_release);

    return true;
}

int AsusFanControl::setFanMode(uint8_t index, uint32_t mode) {
    if (index > 1) {
        return -1;
    }
    if (index == 0 && !isFanPresent0) {
        return -1;
    }
    if (index == 1 && !isFanPresent1) {
        return -1;
    }

    uint32_t dev_id = index ? ASUS_WMI_DEVID_CPU_FAN2_CTRL : ASUS_WMI_DEVID_CPU_FAN_CTRL;
    uint32_t value;

    switch (mode) {
        case ASUS_FAN_CTRL_FULLSPEED:
            // DEVS value 1 sets bit 0x40 in the EC fan-control page, which is
            // the firmware's own full-speed state. Verified on a GA503QM:
            // fan0 goes from ~2400 to ~6800 RPM and returns on value 0.
            value = 1;
            break;

        case ASUS_FAN_CTRL_AUTO:
            // DEVS value 0 clears the bit; the EC curve takes over again.
            value = 0;
            break;

        default:
            // No per-fan manual PWM on boards without the 0x00110012 device.
            return -1;
    }

    int ret = wmiEvaluateMethod(ASUS_WMI_METHODID_DEVS, dev_id, value);
    if (ret == -1) {
        DBGLOG("fan", "setFanMode: DEVS failed for fan %u", index);
        return -1;
    }

    atomic_store_explicit(index ? &fanMode1 : &fanMode0,
                          mode == ASUS_FAN_CTRL_FULLSPEED ? ASUS_FAN_CTRL_FULLSPEED
                                                          : ASUS_FAN_CTRL_AUTO,
                          memory_order_release);

    DBGLOG("fan", "setFanMode: fan %u -> %u (ret %d)", index, mode, ret);
    return 0;
}

int AsusFanControl::setThermalProfile(uint32_t profile) {
    if (profile != ASUS_THERMAL_POLICY_BALANCED &&
        profile != ASUS_THERMAL_POLICY_PERFORMANCE &&
        profile != ASUS_THERMAL_POLICY_SILENT) {
        DBGLOG("fan", "refusing invalid thermal profile %u", profile);
        return -1;
    }

    if (!isThermalPolicyPresent) {
        DBGLOG("fan", "0x00120075 is not present on this board");
        return -1;
    }

    int ret = wmiEvaluateMethod(ASUS_WMI_METHODID_DEVS,
                                ASUS_WMI_DEVID_THROTTLE_THERMAL_POLICY, profile);
    if (ret == -1) {
        DBGLOG("fan", "DEVS failed for thermal profile %u", profile);
        return -1;
    }
    if (ret != 1) {
        DBGLOG("fan", "unexpected DEVS result %d for thermal profile", ret);
        return -1;
    }

    atomic_store_explicit(&thermalProfile, profile, memory_order_release);
    return 0;
}

uint32_t AsusFanControl::getThermalProfile() const {
    // The DSDT answers DSTS for 0x00120075 with a hardcoded 0x00010003 and never
    // reports the live mode, so reading it back tells us nothing. Report what
    // was last written; on a fresh boot that is the balanced default, matching
    // the firmware's power-on state.
    return atomic_load_explicit(&thermalProfile, memory_order_acquire);
}

// --- SMC key registration ---------------------------------------------------
//
// The keys are registered when VirtualSMC hands us the plugin submission, the
// same path AsusSMC uses. Each value holds a reference to the live service so it
// can reach the ACPI device.

void registerFanKeys(AsusFanControl *drv, VirtualSMCAPI::Plugin *plugin) {
    if (!drv || !plugin) {
        return;
    }

    VirtualSMCAPI::addKey(KeyFNum, plugin->data, VirtualSMCAPI::valueWithUint8(
        2, nullptr,
        SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));

    VirtualSMCAPI::addKey(KeyF0Ac, plugin->data, VirtualSMCAPI::valueWithFp(
        0, SmcKeyTypeFpe2, new F0Ac(&drv->fanSpeed0),
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyF0Mn, plugin->data, VirtualSMCAPI::valueWithFp(
        0, SmcKeyTypeFpe2, new FConstAc(kFanMinRPM),
        SMC_KEY_ATTRIBUTE_READ));
    VirtualSMCAPI::addKey(KeyF0Mx, plugin->data, VirtualSMCAPI::valueWithFp(
        0, SmcKeyTypeFpe2, new FConstAc(kFanMaxRPM),
        SMC_KEY_ATTRIBUTE_READ));

    VirtualSMCAPI::addKey(KeyF0Md, plugin->data, VirtualSMCAPI::valueWithUint8(
        ASUS_FAN_CTRL_AUTO, new FMode(drv, 0, &drv->fanMode0),
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    FanTypeDescStruct desc;
    lilu_os_strncpy(desc.strFunction, "System Fan", DiagFunctionStrLen);

    VirtualSMCAPI::addKey(KeyF0ID, plugin->data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&desc), sizeof(desc), SmcKeyTypeFds, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_FUNCTION));

    // Second fan. Present on this board (fan1 reports its own RPM); the shapes
    // are identical apart from the location tag.
    VirtualSMCAPI::addKey(KeyF1Ac, plugin->data, VirtualSMCAPI::valueWithFp(
        0, SmcKeyTypeFpe2, new F1Ac(&drv->fanSpeed1),
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_FUNCTION));

    VirtualSMCAPI::addKey(KeyF1Mn, plugin->data, VirtualSMCAPI::valueWithFp(
        0, SmcKeyTypeFpe2, new FConstAc(kFanMinRPM),
        SMC_KEY_ATTRIBUTE_READ));
    VirtualSMCAPI::addKey(KeyF1Mx, plugin->data, VirtualSMCAPI::valueWithFp(
        0, SmcKeyTypeFpe2, new FConstAc(kFanMaxRPM),
        SMC_KEY_ATTRIBUTE_READ));

    VirtualSMCAPI::addKey(KeyF1Md, plugin->data, VirtualSMCAPI::valueWithUint8(
        ASUS_FAN_CTRL_AUTO, new FMode(drv, 1, &drv->fanMode1),
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    FanTypeDescStruct desc1;
    lilu_os_strncpy(desc1.strFunction, "System Fan", DiagFunctionStrLen);
    desc1.location = LEFT_UPPER_REAR;

    VirtualSMCAPI::addKey(KeyF1ID, plugin->data, VirtualSMCAPI::valueWithData(
        reinterpret_cast<const SMC_DATA *>(&desc1), sizeof(desc1), SmcKeyTypeFds, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_FUNCTION));

    // The thermal profile applies to both fans, so it is a single key.
    VirtualSMCAPI::addKey(KeyF0Tp, plugin->data, VirtualSMCAPI::valueWithUint8(
        ASUS_THERMAL_POLICY_BALANCED, new FThermalProfile(drv),
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));
}

void AsusFanControl::registerVSMC() {
    vsmcNotifier = VirtualSMCAPI::registerHandler(&AsusFanControl::vsmcNotificationHandler, this);
}

bool AsusFanControl::vsmcNotificationHandler(void *sensors, void *refCon,
                                             IOService *vsmc, IONotifier *notifier) {
    if (!sensors || !vsmc) {
        return false;
    }

    auto self = static_cast<AsusFanControl *>(sensors);
    auto ret = vsmc->callPlatformFunction(VirtualSMCAPI::SubmitPlugin, true,
                                         sensors, &self->vsmcPlugin, nullptr, nullptr);
    if (ret != kIOReturnSuccess) {
        SYSLOG("fan", "plugin submission failed");
        return false;
    }

    DBGLOG("fan", "plugin submitted");
    registerFanKeys(self, &self->vsmcPlugin);
    return true;
}

EXPORT extern "C" kern_return_t ADDPR(kern_start)(kmod_info_t *, void *) {
    PE_parse_boot_argn("liludelay", &ADDPR(debugPrintDelay), sizeof(ADDPR(debugPrintDelay)));
    ADDPR(debugEnabled) = checkKernelArgument("-vsmcdbg") ||
                          checkKernelArgument("-asussmcdbg") ||
                          checkKernelArgument("-asusfandbg");
    return KERN_SUCCESS;
}

EXPORT extern "C" kern_return_t ADDPR(kern_stop)(kmod_info_t *, void *) {
    return KERN_SUCCESS;
}
