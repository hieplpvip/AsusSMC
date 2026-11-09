//
//  AsusSMC.cpp
//  AsusSMC
//
//  Copyright 婕?2018-2020 Le Bao Hiep. All rights reserved.
//

#include "AsusSMC.hpp"

#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/IOMemoryDescriptor.h>

bool ADDPR(debugEnabled) = false;
uint32_t ADDPR(debugPrintDelay) = 0;

#define super IOService
OSDefineMetaClassAndStructors(AsusSMC, IOService)

bool AsusSMC::init(OSDictionary *dict) {
    if (!super::init(dict)) {
        return false;
    }

    _notificationServices = OSSet::withCapacity(1);

    kev.setVendorID("com.hieplpvip");
    kev.setEventCode(AsusSMCEventCode);

    atomic_init(&currentLux, 0);

    return true;
}

IOService *AsusSMC::probe(IOService *provider, SInt32 *score) {
    if (!super::probe(provider, score)) {
        return NULL;
    }

    IOACPIPlatformDevice *dev = OSDynamicCast(IOACPIPlatformDevice, provider);
    if (!dev) {
        return NULL;
    }

    OSObject *obj;
    dev->evaluateObject("_UID", &obj);

    OSString *name = OSDynamicCast(OSString, obj);
    if (!name) {
        return NULL;
    }

    IOService *ret = NULL;
    if (name->isEqualTo("ATK")) {
        ret = this;
    }
    name->release();

    return ret;
}

bool AsusSMC::start(IOService *provider) {
    if (!provider || !super::start(provider)) {
        SYSLOG("atk", "failed to start parent");
        return false;
    }

    atkDevice = (IOACPIPlatformDevice *)provider;

    parse_WDG();

    initATKDevice();

    initALSDevice();

    initEC0Device();

    initBattery();

    // Version badge. Every diagnostic below is worthless without knowing which
    // build produced it: a boot that silently kept an older kext read exactly
    // like a failed experiment twice already. Bump kAsusSMCBuildTag whenever
    // behaviour changes and check it with
    //   ioreg -rn AsusSMC -l | grep AsusSMCBuild
    setProperty("AsusSMCBuild", kAsusSMCBuildTag);

    // -asussmcnofollow disables the brightness follow loop.
    blFollowDisabled = checkKernelArgument("-asussmcnofollow");

    // VirtualAppleKeyboard publishes an Apple Vendor KeyboardBacklight
    // collection (usage page 0xFF00, usage 0x0F, report id 191), which is what
    // makes System Settings show the keyboard brightness panel at all. -asussmcnokbd
    // leaves it out, so the panel source can be confirmed in one boot.
    if (!checkKernelArgument("-asussmcnokbd")) {
        initVirtualKeyboard();
    } else {
        setProperty("AsusSMCVirtualKeyboard", "disabled by boot-arg");
    }

    startATKDevice();

    workloop = getWorkLoop();
    if (!workloop) {
        DBGLOG("atk", "Failed to get workloop");
        return false;
    }
    workloop->retain();

    command_gate = IOCommandGate::commandGate(this);
    if (!command_gate || (workloop->addEventSource(command_gate) != kIOReturnSuccess)) {
        DBGLOG("atk", "Could not open command gate");
        return false;
    }

    setProperty("IsTouchpadEnabled", true);
    setProperty("Copyright", "Copyright 婕?2018-2020 Le Bao Hiep. All rights reserved.");

    extern kmod_info_t kmod_info;
    setProperty("AsusSMC-Version", kmod_info.version);
#ifdef DEBUG
    setProperty("AsusSMC-Build", "Debug");
#else
    setProperty("AsusSMC-Build", "Release");
#endif

    registerNotifications();

    registerVSMC();

    // System power events. Ported from upstream PR #62, merged into 1.2.2 and
    // confirmed by several users: without joining the power tree the keyboard
    // stays dark after a wake, because nothing restores the level.
    subscribePowerEvents(provider);

    registerService();

    return true;
}

/**
 *  System power events, ported from upstream PR #62 (merged into 1.2.2).
 *
 *  Two states are enough: off and on. When the system goes to sleep the
 *  keyboard is blanked, and on wake the remembered level is restored. Without
 *  this the keyboard either stays lit during sleep or comes back dark after
 *  waking, because nothing else restores it.
 */
// kIOPMPowerOff does not exist in IOKit; upstream's patch defines its own zero
// for the off state, and so does this one. kIOPMPowerOn does exist.
#define kPowerStateOff 0
#define kPowerStateCount 2

static IOPMPowerState powerStates[kPowerStateCount] = {
    {1, kPowerStateOff, kPowerStateOff, kPowerStateOff, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0},
};

IOReturn AsusSMC::setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) {
    // Guard added upstream in v1.2.3 ("Correct usage of powerStateOrdinal in
    // setPowerState"). setPowerState is also delivered for other devices in
    // the power tree; without this check an unrelated transition would blink
    // the keyboard. PR #62 as originally merged lacked it.
    if (whatDevice != this) {
        return kIOPMAckImplied;
    }

    if (powerStateOrdinal == kPowerStateOff) {
        // Sleep, shutdown or reboot. Blanking here is what keeps the keyboard
        // from glowing through the night.
        int ret = sendKbdBacklightViaHID(0);
        SYSLOG("kbd", "system sleep -> keyboard backlight off (ret=0x%X)", (unsigned)ret);
    } else {
        int ret = sendKbdBacklightViaHID(kbdLevel);
        SYSLOG("kbd", "system wake -> keyboard backlight %u (ret=0x%X)",
               (unsigned)kbdLevel, (unsigned)ret);
    }
    return kIOPMAckImplied;
}

void AsusSMC::subscribePowerEvents(IOService *provider) {
    PMinit();
    provider->joinPMtree(this);
    IOReturn ret = registerPowerDriver(this, powerStates, kPowerStateCount);
    pmJoined = (ret == kIOReturnSuccess);
    setProperty("AsusSMCPowerEvents", pmJoined ? "registered" : "failed");
    SYSLOG("kbd", "power events: registerPowerDriver ret=0x%X", (unsigned)ret);
}

void AsusSMC::stop(IOService *provider) {
    if (pmJoined) {
        PMstop();
        pmJoined = false;
    }
    if (poller) {
        poller->cancelTimeout();
    }
    if (workloop && poller) {
        workloop->removeEventSource(poller);
    }
    if (workloop && command_gate) {
        workloop->removeEventSource(command_gate);
    }
    OSSafeReleaseNULL(workloop);
    OSSafeReleaseNULL(poller);
    OSSafeReleaseNULL(command_gate);

    _publishNotify->remove();
    _terminateNotify->remove();
    _notificationServices->flushCollection();
    OSSafeReleaseNULL(_publishNotify);
    OSSafeReleaseNULL(_terminateNotify);
    OSSafeReleaseNULL(_notificationServices);

    OSSafeReleaseNULL(kbdDevice);

    super::stop(provider);
    return;
}

IOReturn AsusSMC::message(uint32_t type, IOService *provider, void *argument) {
    // argument is null for some messages - notably the power management ones
    // added below - so it must not be dereferenced unconditionally. DBGLOG
    // evaluates its arguments even when logging is off, so this would have
    // panicked a Debug build on the first display transition.
    DBGLOG("atk", "Received message: %u Type %x Provider %s",
           argument ? *((uint32_t *)argument) : 0, type,
           provider ? provider->getName() : "unknown");

    switch (type) {
        case kIOMessageDeviceWillPowerOff:
        {
            // Delivered because we registered as an interested driver of the
            // display power manager; this is the display going to sleep. It is
            // the signal the registry could never provide, and it means the
            // keyboard can be blanked without macOS driving the backlight.
            displayOffEvents++;
            publishDisplayEvents();

            if (hasKeyboardBacklight) {
                int ret = sendKbdBacklightViaHID(0);
                kbdDimmedForDisplay = true;
                SYSLOG("kbd", "display power off -> keyboard backlight off (ret=0x%X)",
                       (unsigned)ret);
            }
            break;
        }

        case kIOMessageDeviceHasPoweredOn:
        {
            displayOnEvents++;
            publishDisplayEvents();

            if (hasKeyboardBacklight) {
                int ret = sendKbdBacklightViaHID(kbdLevel);
                kbdDimmedForDisplay = false;
                SYSLOG("kbd", "display powered on -> keyboard backlight %u (ret=0x%X)",
                       (unsigned)kbdLevel, (unsigned)ret);
            }
            break;
        }

        case kIOACPIMessageDeviceNotification:
        {
            // Every ACPI notification lands here, not just the key events.
            // Counting them separately is what distinguishes "the firmware
            // never notifies us at all" from "it notifies, but the codes are
            // not what handleMessage expects".
            notifyCount++;
            {
                char rec[96];
                snprintf(rec, sizeof(rec), "notify=%u raw=0x%X direct=%d",
                         (unsigned)notifyCount,
                         argument ? (unsigned)*((uint32_t *)argument) : 0,
                         directACPImessaging ? 1 : 0);
                setProperty("AsusSMCNotify", rec);
            }

            if (directACPImessaging) {
                handleMessage(*((uint32_t *)argument));
            } else {
                // Dequeue with GANQ rather than _WED.
                //
                // _WED only pops when its argument is exactly 0xFF:
                //
                //   Method (_WED, 1) { If ((Arg0 == 0xFF)) { Return (GANQ()) }
                //                      Return (Ones) }
                //
                // On this machine the 0xFF never survives the call, so every
                // evaluateInteger("_WED", ...) falls through to "Return (Ones)"
                // and the driver was handed 0xFFFFFFFF instead of a key code.
                // Measured: AQNO sat at 16 - the ring's capacity - across two
                // separate boots, which means IANE enqueued 16 codes and
                // nothing ever dequeued them.
                //
                // GANQ takes no arguments, so there is nothing to lose on the
                // way in, and it answers Ones exactly when the ring is empty.
                uint32_t res = 0;
                IOReturn kr = atkDevice->evaluateInteger("GANQ", &res);
                if (kr == kIOReturnSuccess && res != 0xFFFFFFFF) {
                    handleMessage((int)res);
                }
            }
            break;
        }

        case kSetKeyboardBacklightMessage:
        {
            if (hasKeyboardBacklight) {
                // The caller (SMCKBrdBLightValue::update) has already mapped
                // the macOS 0..0xFFB range onto 0..ASUS_KBD_BACKLIGHT_MAX.
                uint8_t level = (uint8_t)(*((uint16_t *)argument));
                if (level > ASUS_KBD_BACKLIGHT_MAX) {
                    level = ASUS_KBD_BACKLIGHT_MAX;
                }

                // A level of 0 arriving here is ignored.
                //
                // Verified on this machine: macOS never drives LKSB, but
                // VirtualSMC calls update() with the key's default initialiser
                // at registration, and the default {val1 = 0, val2 = 1} maps
                // to level 0. Acting on it turned the keyboard off at boot.
                //
                // The display-following path writes 0 directly when the panel
                // sleeps, and asus-kbd can still turn the light off, so
                // nothing is lost by not honouring a stray zero here.
                if (level == 0) {
                    setProperty("AsusSMCLastBacklight", "level=0 ignored (SMC path)");
                    SYSLOG("kbd", "ignoring backlight level 0 from the SMC path");
                    break;
                }

                // While the display is asleep the driver owns the backlight.
                // macOS's auto-brightness may write a level right after we
                // blanked the keyboard (the ALS keeps reading), which relights
                // it immediately - the "display sleeps but the keyboard stays
                // lit" report. Those writes are recorded but not applied.
                if (backlightLockedWhileDimmed && level != 0) {
                    lockedRejectCount++;
                    char lock[96];
                    snprintf(lock, sizeof(lock),
                             "locked while dimmed: rejected level=%u (total=%u)",
                             (unsigned)level, (unsigned)lockedRejectCount);
                    setProperty("AsusSMCLastBacklight", lock);
                    SYSLOG("kbd", "%s", lock);
                    break;
                }

                // Remember it: followDisplayPower() restores this after the
                // panel comes back.
                kbdLevel = level;

                // A request from the system while the panel is dark means the
                // two are no longer in sync; let the next poll decide.
                kbdDimmedForDisplay = false;

                if (keyboardBacklightViaWMI) {
                    // Preferred route: USB HID feature report. This is what
                    // Linux (hid-asus) and Windows both use, and it is the
                    // only path that actually reaches the EC on this board.
                    int hidRet = sendKbdBacklightViaHID(level);

                    char rec[160];
                    if (hidRet == kIOReturnSuccess) {
                        snprintf(rec, sizeof(rec),
                                 "level=%u path=HID ok", level);
                        setProperty("AsusSMCLastBacklight", rec);
                        SYSLOG("kbd", "keyboard backlight %s", rec);
                        break;
                    }

                    // Fall back to ACPI. Matches Linux asus-wmi's
                    // kbd_led_update(): ctrl_param = 0x80 | (level & 0x7F).
                    // The 0x80 bit is what EC0.SLKB tests; it is expected to be
                    // a no-op on this DSDT, but keep it for other boards.
                    uint32_t ctrl = ASUS_KBD_BACKLIGHT_ON_BIT | (level & 0x7F);
                    int ret = wmi_evaluate_method(ASUS_WMI_METHODID_DEVS,
                                                  ASUS_WMI_DEVID_KEYBOARD_BACKLIGHT,
                                                  ctrl);

                    snprintf(rec, sizeof(rec),
                             "level=%u path=ACPI hidRet=0x%X ctrl=0x%X devsRet=%d",
                             level, (unsigned)hidRet, ctrl, ret);
                    setProperty("AsusSMCLastBacklight", rec);
                    SYSLOG("kbd", "keyboard backlight %s", rec);
                } else {
                    OSNumber *arg = OSNumber::withNumber(level, 16);
                    IOReturn kr = atkDevice->evaluateObject("SKBV", NULL, (OSObject **)&arg, 1);
                    arg->release();

                    char rec[96];
                    snprintf(rec, sizeof(rec), "level=%u skbvRet=0x%X",
                             level, (unsigned)kr);
                    setProperty("AsusSMCLastBacklight", rec);
                    SYSLOG("kbd", "keyboard backlight %s", rec);
                }
            }
            break;
        }

        default:
            return kIOReturnInvalid;
    }
    return kIOReturnSuccess;
}

int AsusSMC::wmi_parse_guid(const char *in, char *out) {
    for (int i = 3; i >= 0; i--) {
        out += snprintf(out, 3, "%02X", in[i] & 0xFF);
    }

    out += snprintf(out, 2, "-");
    out += snprintf(out, 3, "%02X", in[5] & 0xFF);
    out += snprintf(out, 3, "%02X", in[4] & 0xFF);
    out += snprintf(out, 2, "-");
    out += snprintf(out, 3, "%02X", in[7] & 0xFF);
    out += snprintf(out, 3, "%02X", in[6] & 0xFF);
    out += snprintf(out, 2, "-");
    out += snprintf(out, 3, "%02X", in[8] & 0xFF);
    out += snprintf(out, 3, "%02X", in[9] & 0xFF);
    out += snprintf(out, 2, "-");

    for (int i = 10; i <= 15; i++) {
        out += snprintf(out, 3, "%02X", in[i] & 0xFF);
    }

    *out = '\0';
    return 0;
}

int AsusSMC::wmi_evaluate_method(uint32_t method_id, uint32_t arg0, uint32_t arg1) {
    OSObject *params[3];

    struct wmi_args args = {
        .arg0 = arg0,
        .arg1 = arg1
    };

    params[0] = OSNumber::withNumber(static_cast<uint32_t>(0), 32);
    params[1] = OSNumber::withNumber(method_id, 32);
    params[2] = OSData::withBytes(&args, sizeof(wmi_args));

    uint32_t val;
    IOReturn ret = atkDevice->evaluateInteger(wmi_method, &val, params, 3);
    params[0]->release();
    params[1]->release();
    params[2]->release();

    if (ret != kIOReturnSuccess) {
        DBGLOG("wmi", "wmi_evaluate_method failed");
        return -1;
    }

    if (val == 0xfffffffe) {
        DBGLOG("wmi", "wmi_evaluate_method invalid method_id");
        return -1;
    }

    return val;
}

int AsusSMC::wmi_get_devstate(uint32_t dev_id) {
    return wmi_evaluate_method(ASUS_WMI_METHODID_DSTS, dev_id, 0);
}

bool AsusSMC::wmi_dev_is_present(uint32_t dev_id) {
    int status = wmi_evaluate_method(ASUS_WMI_METHODID_DSTS, dev_id, 0);
    return status != -1 && (status & ASUS_WMI_DSTS_PRESENCE_BIT);
}

/**
 *  Detect the "generic WMI backlight" shape of the keyboard backlight.
 *
 *  Boards like the GA503QM have no SKBV method; their DSDT routes the
 *  backlight through DEVS device 0x00050021 -> EC0.SLKB. That device has
 *  no DSTS branch at all, so wmi_dev_is_present() cannot see it and asking
 *  DSTS just returns 0xFFFFFFFE.
 *
 *  Presence therefore has to be established structurally: the write has to
 *  go through ATKD.WMNB's DEVS chain, which only offers 0x00050021 when the
 *  board implements it. We confirm by writing "off" once; on a board without
 *  the device DEVS reports unsupported and nothing happens. Off is the safe
 *  value to probe with.
 */
bool AsusSMC::pci0Ec0HasKeyboardBacklight() {    if (wmi_dev_is_present(ASUS_WMI_DEVID_KEYBOARD_BACKLIGHT)) {
        return true;
    }

    int ret = wmi_evaluate_method(ASUS_WMI_METHODID_DEVS,
                                  ASUS_WMI_DEVID_KEYBOARD_BACKLIGHT,
                                  ASUS_KBD_BACKLIGHT_ON_BIT | 0);
    if (ret == -1 || ret == (int)ASUS_WMI_UNSUPPORTED_METHOD) {
        return false;
    }

    // DEVS on this chain answers 1 on success.
    return ret == 1;
}

/**
 *  Locate the ASUS keyboard's HID device.
 *
 *  The backlight cannot be reached over ACPI on this board (EC0.SLKB is a
 *  no-op), so the level has to go out as a USB HID feature report. Feature
 *  reports travel over the control endpoint, which is device wide, so any of
 *  the keyboard's HID interfaces will do - matching on the vendor id alone is
 *  enough and avoids depending on which collection happens to carry 0xFF31.
 *
 *  Resolution is lazy and retried: this driver matches an ACPI device, which
 *  is up long before the USB keyboard enumerates.
 */
bool AsusSMC::ensureKbdHIDDevice() {
    if (kbdHIDDevice) {
        return true;
    }

    OSDictionary *matching = IOService::serviceMatching("IOHIDDevice");
    if (!matching) {
        return false;
    }

    OSNumber *vid = OSNumber::withNumber((unsigned long long)ASUS_USB_VENDOR_ID, 32);
    if (vid) {
        matching->setObject("VendorID", vid);
        vid->release();
    }

    // Short timeout: this can be called from the SMC write path.
    IOService *svc = IOService::waitForMatchingService(matching, 100000000ULL /* 100 ms */);
    matching->release();

    if (!svc) {
        return false;
    }

    IOHIDDevice *hid = OSDynamicCast(IOHIDDevice, svc);
    if (hid) {
        hid->retain();
        kbdHIDDevice = hid;
        SYSLOG("kbd", "keyboard HID device found: %s", svc->getName());
    }
    svc->release();

    return kbdHIDDevice != nullptr;
}

/**
 *  Send the backlight feature report.
 *
 *  Layout matches drivers/hid/hid-asus.c asus_kbd_set_brightness():
 *    5A BA C5 C4 <level>   padded to 16 bytes
 *  and ROG-HID, which sends the same buffer through DriverKit.
 *
 *  @return kIOReturnSuccess when the report left the machine.
 */
int AsusSMC::sendKbdBacklightViaHID(uint8_t level) {
    if (!ensureKbdHIDDevice()) {
        return kIOReturnNotReady;
    }

    // The keyboard's report descriptor for collection 0xFF31/0x76 declares,
    // for report id 0x5A, a 5-byte input report and a 63-byte feature report
    // (MaxFeatureReportSize reports 64, counting the id byte). The payload is
    // therefore padded to 64 bytes, which is also the length that is proven to
    // work: the userspace tool drove the backlight with exactly this layout.
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x5A;   // report id
    buf[1] = 0xBA;   // command
    buf[2] = 0xC5;   // illumination down
    buf[3] = 0xC4;   // illumination up
    buf[4] = level;

    IOBufferMemoryDescriptor *report =
        IOBufferMemoryDescriptor::withBytes(buf, sizeof(buf), kIODirectionOut);
    if (!report) {
        return kIOReturnNoMemory;
    }

    // The report id also goes in the low 8 bits of options; this is how
    // IOHIDDevice::setReport is documented.
    IOReturn kr = kbdHIDDevice->setReport(report, kIOHIDReportTypeFeature, 0x5A);
    report->release();

    return (int)kr;
}

void AsusSMC::recordKbdWrite(uint8_t val1, uint8_t val2, uint16_t packed, uint16_t level) {
    kbdWriteCount++;

    // Keep a short history of the raw payloads, not just the last one.
    //
    // macOS writes LKSB repeatedly at boot - 19 times on this machine. With
    // only the final value recorded there is no way to tell whether that is
    // nineteen identical zeros or a sequence, and the difference decides
    // whether macOS is sweeping the range or repeatedly giving up.
    if (kbdWriteLogPos < sizeof(kbdWriteLog) - 8) {
        kbdWriteLogPos += (size_t)snprintf(kbdWriteLog + kbdWriteLogPos,
                                          sizeof(kbdWriteLog) - kbdWriteLogPos,
                                          "%u,%u;", val1, val2);
    }

    char rec[160];
    snprintf(rec, sizeof(rec),
             "val1=%u val2=%u packed=0x%X level=%u count=%u log=%s",
             val1, val2, packed, level, (unsigned)kbdWriteCount, kbdWriteLog);
    setProperty("AsusSMCLKSB", rec);
}

void AsusSMC::recordLKSAccess(uint32_t key, bool isWrite, uint16_t value) {
    if (key > 1) {
        return;
    }
    if (isWrite) {
        lksWrites[key]++;
        lksLastValue = value;
    } else {
        lksReads[key]++;
    }
    publishLKSAccess();
}

void AsusSMC::publishLKSAccess() {
    // Published even when every counter is zero, which is exactly the case
    // that needs diagnosing: a keyboard brightness control that never touches
    // these keys.
    char rec[160];
    snprintf(rec, sizeof(rec),
             "lksbR=%u lksbW=%u lkssR=%u lkssW=%u lastW=0x%X",
             (unsigned)lksReads[0], (unsigned)lksWrites[0],
             (unsigned)lksReads[1], (unsigned)lksWrites[1],
             (unsigned)lksLastValue);
    setProperty("AsusSMCLKSAccess", rec);
}

void AsusSMC::parse_WDG() {
    // Diagnostic: report what _WDG actually contains. Without this, a wrong
    // WMI method name looks exactly like "the firmware never sends events",
    // because every call quietly fails.
    wmiMethodFound = false;

    OSObject *wdg;
    if (atkDevice->evaluateObject("_WDG", &wdg) != kIOReturnSuccess) {
        SYSLOG("wmi", "No method _WDG!");
        setProperty("AsusSMCWDG", "no _WDG method");
        return;
    }

    OSData *data = OSDynamicCast(OSData, wdg);
    if (!data) {
        SYSLOG("guid", "Cast WDG error!");
        setProperty("AsusSMCWDG", "cast failed");
        return;
    }

    int total = data->getLength() / sizeof(struct guid_block);

    char guid_string[37];
    char diag[160];
    snprintf(diag, sizeof(diag), "len=%d blocks=%d", (int)data->getLength(), total);

    for (int i = 0; i < total; i++) {
        struct guid_block *g = (struct guid_block *) data->getBytesNoCopy(i * sizeof(struct guid_block), sizeof(struct guid_block));
        wmi_parse_guid(g->guid, guid_string);

        if (i == 0) {
            snprintf(diag, sizeof(diag), "len=%d blocks=%d first=%s obj=%c%c",
                     (int)data->getLength(), total, guid_string,
                     g->object_id[0], g->object_id[1]);
        }

        if (strncmp(guid_string, ASUS_WMI_MGMT_GUID, 36) == 0) {
            snprintf(wmi_method, 5, "WM%c%c", g->object_id[0], g->object_id[1]);
            wmiMethodFound = true;
            snprintf(diag, sizeof(diag), "len=%d blocks=%d matched=%s method=%s",
                     (int)data->getLength(), total, guid_string, wmi_method);
            setProperty("AsusSMCWDG", diag);
            DBGLOG("wmi", "parse_WDG found WMI method %s", wmi_method);
            return;
        }
    }

    // Couldn't find WMI method. Let's assume it's WMNB
    SYSLOG("wmi", "parse_WDG couldn't find WMI method");
    lilu_os_strncpy(wmi_method, "WMNB", 5);
    snprintf(diag, sizeof(diag), "%s (no GUID match, assuming WMNB)", diag);
    setProperty("AsusSMCWDG", diag);
}

void AsusSMC::initATKDevice() {
    // Diagnostic: the return value decides everything downstream. ATKD.INIT
    // sets ATKP = One, and every _Qxx handler in this DSDT is wrapped in
    // `If (ATKP)`. If this call fails, no Fn key can ever produce an event,
    // and the failure is otherwise completely silent.
    int ret = wmi_evaluate_method(ASUS_WMI_METHODID_INIT, 0, 0);

    char rec[128];
    snprintf(rec, sizeof(rec), "INIT ret=%d method=%s guidMatched=%d",
             ret, wmi_method, wmiMethodFound ? 1 : 0);
    setProperty("AsusSMCInitATK", rec);
    SYSLOG("atk", "ATKD.INIT -> %d (method %s, guid matched %d)",
           ret, wmi_method, wmiMethodFound ? 1 : 0);
}

/**
 *  Report whether the ATKD event path answers, and periodically prove that the
 *  delivery path itself works.
 *
 *  _WED(0xFF) pops the queue: Ones means empty right now.
 *
 *  A permanently empty queue has two completely different causes:
 *
 *    * the EC never raises its interrupt, so no _Qxx runs, so IANE is never
 *      called and nothing is ever queued;
 *    * the queue/delivery path is broken even when IANE does run.
 *
 *  Nothing else distinguishes them, so once every few seconds this calls
 *  ATKD.IANE(0xC6) directly. 0xC6 is chosen because handleMessage already
 *  ignores it ("ALS notifications"), so the call has no side effects at all.
 *
 *  If the injected code comes back out of _WED, the whole path from IANE
 *  through IANQ and Notify to message() is intact, and the empty queue is
 *  purely an EC-side problem. If it does not come back, the break is inside
 *  ACPI and not in the EC.
 */
/**
 *  Observe the ATKD event queue without consuming it.
 *
 *  Measured on the machine: notify went 9 -> 16 while Fn keys were pressed,
 *  raw=0xFF every time. The only Notify(ATKD, 0xFF) in the whole DSDT sits
 *  inside IANE, so IANE runs on every key press and the event chain is alive.
 *  Yet _WED keeps answering Ones.
 *
 *  The earlier build tried to settle this by calling GANQ() every second.
 *  That was wrong, and it may itself be the cause of what it was measuring:
 *  GANQ is the dequeue. A poller that runs every second and pops one entry
 *  competes with message(), which pops the same queue via _WED. Whichever
 *  runs first gets the code, so the notification path could be starved by
 *  the diagnostic that was supposed to observe it.
 *
 *  AQNO is the queue's entry count and reading it changes nothing, so that is
 *  what is used now. The three states are distinguishable:
 *
 *    aqno rising as keys are pressed  -> IANQ stores codes correctly, and the
 *                                        problem is purely in dequeueing;
 *    aqno stays 0                     -> IANQ never stores anything, so the
 *                                        code is lost at the enqueue step;
 *    aqno unavailable                 -> the name is not visible from here.
 *
 *  IANQ is still exercised every few seconds, but it only enqueues and never
 *  pops - the injection uses IANQ rather than IANE because IANE has no Return
 *  statement, so evaluating it as an integer can only yield
 *  kIOReturnBadArgument (0xE00002C2).
 */
/**
 *  Drain the ATKD event queue and route each code through handleMessage.
 *
 *  This replaces the diagnostic that read AQNO and injected test codes. Both
 *  of those were wrong, and the second one was actively harmful:
 *
 *    * injecting IANQ(0xC6) every five seconds filled the ring. ATKQ holds
 *      only 16 entries and IANQ blocks for up to a second and then returns
 *      Zero once it is full, so after about 80 seconds the whole EC event
 *      path was stalled - which is why notify dropped to 0 and no key did
 *      anything at all.
 *    * reading AQNO told us the depth but nothing about the codes, and the
 *      queue cannot be left full or nothing new is ever accepted.
 *
 *  The queue was observed full (aqno=16) which proves the enqueue side works:
 *  IANQ does store codes. The reason message() always received Ones is that
 *  _WED only dequeues when its argument is exactly 0xFF; when that argument
 *  does not survive the call it falls through to "Return (Ones)".
 *
 *  So the queue is drained here directly. GANQ takes no arguments, so nothing
 *  can be lost on the way in, and it returns Ones exactly when the ring is
 *  empty. Every code it produces is handed to handleMessage, the same
 *  function the notification path calls.
 *
 *  Codes with side effects are deliberately not acted on: 0x33/0x34/0x35
 *  blank the display, 0x5E sleeps, 0x6B toggles the touchpad and 0x7D toggles
 *  airplane mode. Draining a backlog while the machine is in use should not
 *  sleep it. They are counted instead, so a key that only produces one of
 *  those still shows up as "seen".
 *
 *  The oldest entry is dropped if the ring is already full, because nothing
 *  else will ever drain it and a full ring stops all further events.
 */
/**
 *  Report the ATKD ring state, and clear it exactly once if it is stalled.
 *
 *  message() now dequeues with GANQ directly, so this must not compete with it.
 *  The previous build ran a GANQ poll once per second and that starved the
 *  notification path - the diagnostic became the bug. So this only reads AQNO
 *  in the steady state.
 *
 *  The one exception is a full ring. ATKQ holds 16 entries and nothing in the
 *  DSDT ever resets AQNO, so a backlog that accumulated before the driver
 *  started draining properly stays there forever. A full ring is worse than
 *  stale data: IANQ sleeps for up to a second and then refuses every new code,
 *  which stalls the entire EC event path. Measured on the last two boots:
 *  AQNO sat at exactly 16 and notify fell to zero.
 *
 *  So a full ring is cleared once. It is reported as "cleared" so the fact that
 *  stale entries were discarded is visible rather than silent. Below 16 the
 *  poller touches nothing at all.
 */
void AsusSMC::probeATKEventPath() {
    // Keep trying to resolve EC0 until it appears. start() runs before
    // AppleACPIPlatformExpert has necessarily enumerated PNP0C09, and one boot
    // found it while the next did not - a plain timing race, not a logic bug.
    if (ec0Pending || !ec0Device) {
        initEC0Device();
    }

    OSObject *aqnoObj = nullptr;
    uint32_t aqno = 0;
    IOReturn aqnoRet = kIOReturnNotFound;

    if (atkDevice) {
        aqnoRet = atkDevice->evaluateObject("AQNO", &aqnoObj);
        if (aqnoRet == kIOReturnSuccess && aqnoObj) {
            if (OSNumber *n = OSDynamicCast(OSNumber, aqnoObj)) {
                aqno = n->unsigned32BitValue();
            }
            aqnoObj->release();
        }
    }

    lastAqno = aqno;

    const char *state = "ok";
    uint32_t dropped = 0;

    // Only ever clear a stalled ring. Anything below capacity may contain
    // codes message() has not consumed yet, and taking those would be stealing
    // events from the notification path.
    if (atkDevice && aqno >= 16) {
        for (int i = 0; i < 16; i++) {
            uint32_t discard = 0;
            IOReturn kr = atkDevice->evaluateInteger("GANQ", &discard);
            if (kr != kIOReturnSuccess || discard == 0xFFFFFFFF) {
                break;
            }
            dropped++;
        }
        overflowDrops += dropped;
        state = "cleared";
    }

    // --- EC0 query path test -----------------------------------------------
    //
    // _Q78 belongs to the embedded controller, not to ATKD:
    //
    //     \_SB.PCI0.SBRG.EC0._Q78      <- the real one
    //     \_SB.ATKD._Q78               <- does not exist
    //
    // It does exactly one thing: If (ATKP) { IANE(0x7C) }. No EC registers are
    // touched, so calling it cannot disturb the machine.
    //
    // A previous boot with qEc=1 produced last=0x7C, count=1 and notify=1 -
    // the whole chain from _Q78 through IANE, IANQ and Notify to message()
    // and GANQ works. qRet came back 0xE00002C2 only because _Q78 has no
    // Return statement, so evaluating it as an integer cannot succeed; the
    // body still ran.
    //
    // That is exactly why this is worth repeating once EC0 is found: if the
    // chain works again, the only remaining question is whether the EC
    // interrupt ever fires on its own.
    if (ec0Device && !qInjected && notifyCount == 0 && injectTicks++ >= 5) {
        uint32_t before = notifyCount;
        uint32_t qret = 0;
        IOReturn qkr = ec0Device->evaluateInteger("_Q78", &qret);

        qCallRet = (uint32_t)qkr;
        qInjected = true;
        qNotifyDelta = notifyCount - before;
    }

    char rec[320];
    snprintf(rec, sizeof(rec),
             "aqnoRet=0x%X aqno=%u %s dropped=%u "
             "qRet=0x%X qRan=%d qDelta=%u qEc=%d "
             "keys=%u notify=%u blocked=%u",
             (unsigned)aqnoRet, (unsigned)lastAqno, state,
             (unsigned)dropped,
             (unsigned)qCallRet, qInjected ? 1 : 0, (unsigned)qNotifyDelta,
             ec0Device ? 1 : 0,
             (unsigned)kbdKeyCount, (unsigned)notifyCount,
             (unsigned)blockedCount);
    setProperty("AsusSMCKeyPath", rec);
}

void AsusSMC::initALSDevice() {
    auto dict = IOService::nameMatching("AppleACPIPlatformExpert");
    if (!dict) {
        SYSLOG("als", "WTF? Failed to create matching dictionary");
        return;
    }

    auto acpi = IOService::waitForMatchingService(dict);
    dict->release();

    if (!acpi) {
        SYSLOG("als", "WTF? No ACPI");
        return;
    }

    acpi->release();

    dict = IOService::nameMatching("ACPI0008");
    if (!dict) {
        SYSLOG("als", "WTF? Failed to create matching dictionary");
        return;
    }

    auto deviceIterator = IOService::getMatchingServices(dict);
    dict->release();

    if (!deviceIterator) {
        SYSLOG("als", "No iterator");
        return;
    }

    alsDevice = OSDynamicCast(IOACPIPlatformDevice, deviceIterator->getNextObject());
    deviceIterator->release();

    if (!alsDevice) {
        SYSLOG("als", "ACPI0008 device not found");
        return;
    }

    if (alsDevice->validateObject("_ALI") != kIOReturnSuccess || !refreshALS(false)) {
        SYSLOG("als", "No functional method _ALI on ALS device");
        return;
    }

    SYSLOG("als", "Found ALS Device %s", alsDevice->getName());
}

void AsusSMC::initEC0Device() {
    // Resolve the embedded controller and read its _CRS.
    //
    // EC0._CRS returns one of two resource sets here, chosen by a GNVS byte
    // (RDHW) that the BIOS sets and nothing in the DSDT ever writes:
    //
    //   RDHW == 0  ->   IO 0x62/0x66 only, no interrupt
    //   RDHW != 0  ->   IO 0x62/0x66 plus GpioInt on pin 0x16
    //
    // SSDT-ECIRQ flips that byte, so _CRS now reports the second set
    // (measured: crsLen 18 -> 53, irq present, gpio 22 = 0x16).
    //
    // Resolution is retried rather than done once. start() runs before
    // AppleACPIPlatformExpert has necessarily finished enumerating PNP0C09,
    // and one boot found the device while the next did not - a plain timing
    // race. When that happens this sets ec0Pending and the sensor poller
    // calls back in until it succeeds.

    if (ec0Device) {
        return;
    }

    auto dict = IOService::nameMatching("PNP0C09");
    if (!dict) {
        setProperty("AsusSMCEC", "no matching dict");
        ec0Pending = true;
        return;
    }

    auto iter = IOService::getMatchingServices(dict);
    dict->release();
    if (!iter) {
        setProperty("AsusSMCEC", "no ec services");
        ec0Pending = true;
        return;
    }

    // getNextObject() returns a borrowed reference, so take one before
    // releasing the iterator. Without this the pointer would be the only
    // thing left pointing at an object nobody retained.
    IOACPIPlatformDevice *ec = OSDynamicCast(IOACPIPlatformDevice, iter->getNextObject());
    if (ec) {
        ec->retain();
    }
    iter->release();

    if (!ec) {
        setProperty("AsusSMCEC", "PNP0C09 not found (will retry)");
        ec0Pending = true;
        return;
    }

    ec0Device = ec;
    ec0Pending = false;
    ec0Retries++;

    OSObject *crs = nullptr;
    IOReturn crsRet = ec->evaluateObject("_CRS", &crs);
    int crsLen = -1;
    if (crsRet == kIOReturnSuccess && crs) {
        if (OSData *d = OSDynamicCast(OSData, crs)) {
            crsLen = (int)d->getLength();
        }
        crs->release();
    }

    // The IO-only set is two 8-byte descriptors; the set carrying the GPIO
    // interrupt adds a GpioInt descriptor and is considerably longer.
    bool hasInterrupt = (crsLen > 0 && crsLen >= 24);

    char rec[200];
    snprintf(rec, sizeof(rec),
             "crsRet=0x%X crsLen=%d irq=%s gpio=%d tries=%u",
             (unsigned)crsRet, crsLen,
             crsLen < 0 ? "unreadable" : (hasInterrupt ? "present" : "MISSING"),
             hasInterrupt ? 0x16 : -1,
             (unsigned)ec0Retries);
    setProperty("AsusSMCEC", rec);
    SYSLOG("ec0", "EC0 _CRS: %s", rec);
}

void AsusSMC::initBattery() {
    // Battery Health was introduced in 10.15.5
    // Check if we're on 10.15.5+
    if (getKernelVersion() < KernelVersion::Catalina || (getKernelVersion() == KernelVersion::Catalina && getKernelMinorVersion() < 5)) {
        return;
    }

    isBatteryRSOCAvailable = wmi_dev_is_present(ASUS_WMI_DEVID_RSOC);
    if (isBatteryRSOCAvailable) {
        toggleBatteryConservativeMode(true);
    }
}

void AsusSMC::initVirtualKeyboard() {
    // Route the KeyboardBacklight collection to this driver before the device
    // starts, so a write from macOS is never dropped for lack of a sink.
    VirtualAppleKeyboard::setBacklightSink(virtualKeyboardBacklightSink, this);

    kbdDevice = new VirtualAppleKeyboard;

    if (!kbdDevice || !kbdDevice->init() || !kbdDevice->attach(this) || !kbdDevice->start(this)) {
        OSSafeReleaseNULL(kbdDevice);
        SYSLOG("vkbd", "Failed to init VirtualAppleKeyboard");
    }
}

void AsusSMC::virtualKeyboardBacklightSink(const uint8_t *bytes, uint32_t length, void *context) {
    AsusSMC *self = static_cast<AsusSMC *>(context);
    if (self) {
        self->handleVirtualKeyboardBacklight(bytes, length);
    }
}

void AsusSMC::handleVirtualKeyboardBacklight(const uint8_t *bytes, uint32_t length) {
    if (!bytes || length < 6) {
        return;
    }

    // The 0xB0 set report, layout from Linux's hid-apple:
    //   bytes[0] = report id (0xB0)
    //   bytes[1] = version   (1)
    //   bytes[2..3] = backlight level (u16, little-endian)
    //   bytes[4..5] = rate   (u16, little-endian)
    //
    // macOS's range goes 0..0x20 (the maximum we advertise in the config
    // report), which maps straight onto the hardware levels 0..3 by dividing
    // by 8 and clamping.
    uint16_t raw = (uint16_t)(bytes[2] | (bytes[3] << 8));

    uint8_t level = (uint8_t)((raw + 4) / 8);  // round to nearest of 0..3
    if (level > ASUS_KBD_BACKLIGHT_MAX) {
        level = ASUS_KBD_BACKLIGHT_MAX;
    }

    // Same lock as the SMC path: while the display is dark, macOS (through the
    // virtual keyboard) may try to relight what we just blanked. Reject before
    // touching kbdLevel or the dimmed flag, so the poller's state stays true.
    if (backlightLockedWhileDimmed && level != 0) {
        lockedRejectCount++;
        SYSLOG("kbd", "HID write locked while dimmed: rejected level=%u (total=%u)",
               (unsigned)level, (unsigned)lockedRejectCount);
        setProperty("AsusSMCHIDBacklight", "locked while dimmed");
        return;
    }

    kbdLevel = level;
    kbdDimmedForDisplay = false;

    // Report back the level on the 0x20 scale the config advertises.
    VirtualAppleKeyboard::reportedLevel = (uint8_t)(level * 8);

    int ret = sendKbdBacklightViaHID(level);
    SYSLOG("kbd", "HID backlight write: level=%u raw=%u (ret=0x%X)",
           (unsigned)level, (unsigned)raw, (unsigned)ret);
    setProperty("AsusSMCHIDBacklight", "applied");
}

void AsusSMC::startATKDevice() {
    // Check direct ACPI messaging support
    if (atkDevice->validateObject("DMES") == kIOReturnSuccess) {
        DBGLOG("atk", "Direct ACPI message is supported");
        setProperty("IsDirectACPIMessagingSupported", kOSBooleanTrue);
        directACPImessaging = true;
    }

    // Check keyboard backlight support.
    //
    // Two shapes exist in the wild:
    //   - a dedicated SKBV method (older Intel ATK DSDTs)
    //   - the generic WMI device 0x00050021, whose DEVS branch calls
    //     EC0.SLKB (GA503QM and similar AMD ROG boards)
    // Only checking SKBV meant the second group was always reported as
    // unsupported, even though the write path works.
    if (atkDevice->validateObject("SKBV") == kIOReturnSuccess) {
        SYSLOG("atk", "Keyboard backlight is supported (SKBV)");
        hasKeyboardBacklight = true;
    } else if (pci0Ec0HasKeyboardBacklight()) {
        SYSLOG("atk", "Keyboard backlight is supported (WMI 0x%08X)",
               ASUS_WMI_DEVID_KEYBOARD_BACKLIGHT);
        hasKeyboardBacklight = true;
        keyboardBacklightViaWMI = true;
    } else {
        hasKeyboardBacklight = false;
        DBGLOG("atk", "Keyboard backlight is not supported");
    }
    setProperty("IsKeyboardBacklightSupported", hasKeyboardBacklight);
    setProperty("IsKeyboardBacklightViaWMI", keyboardBacklightViaWMI);

    // Mirror the probe result into the IORegistry.
    //
    // Lilu's SYSLOG goes through IOLog, which does not reliably reach the
    // unified log on current macOS, so `log show` can come back empty even
    // when the driver is running fine. IORegistry properties always work:
    //
    //   ioreg -rn AsusSMC -l | grep AsusSMC
    //
    // The fan fields are reported by AsusFanControl.kext now, through its own
    // AsusFanControlProbe property.
    {
        char probe[160];
        snprintf(probe, sizeof(probe),
                 "kbd=%d viaWMI=%d direct=%d",
                 hasKeyboardBacklight ? 1 : 0,
                 keyboardBacklightViaWMI ? 1 : 0,
                 directACPImessaging ? 1 : 0);
        setProperty("AsusSMCProbe", probe);
        SYSLOG("atk", "probe summary: %s", probe);
    }

    // Light the keyboard at boot.
    //
    // Once the HID path started working, the previous build showed the
    // keyboard going dark at startup: macOS writes its restored level to LKSB
    // and, with the old broken mapping, that became level 0. The mapping is
    // fixed, but a deliberate default here means the keyboard is never left
    // dark just because the stored preference is 0. macOS may overwrite it a
    // moment later, which is fine.
    if (hasKeyboardBacklight) {
        // Publish the counters before anything else so a zero reading is
        // visible rather than absent.
        publishLKSAccess();

        int kr = sendKbdBacklightViaHID(2);
        char rec[96];
        snprintf(rec, sizeof(rec), "boot default level=2 hidRet=0x%X", (unsigned)kr);
        setProperty("AsusSMCBootBacklight", rec);
        SYSLOG("kbd", "%s", rec);
    }

    // Turn on ALS sensor
    toggleALS(true);
    isALSEnabled = true;
    setProperty("IsALSEnabled", isALSEnabled);
    SYSLOG("atk", "ALS is turned on at boot");
}

bool AsusSMC::refreshALS(bool post) {
    if (!alsDevice) {
        return false;
    }

    IOReturn ret = kIOReturnSuccess;
    uint32_t lux = 150;

    if (isALSEnabled) {
        ret = alsDevice->evaluateInteger("_ALI", &lux);
        if (ret != kIOReturnSuccess) {
            lux = 0xFFFFFFFF; // ACPI invalid
        }
    }

    atomic_store_explicit(&currentLux, lux, memory_order_release);

    if (post) {
        VirtualSMCAPI::postInterrupt(SmcEventALSChange);
        poller->setTimeoutMS(SensorUpdateTimeoutMS);
    }

    DBGLOG("als", "refreshALS lux %u", lux);

    return ret == kIOReturnSuccess;
}

void AsusSMC::handleMessage(int code) {
    // Record every notification that reaches us.
    //
    // This answers a question the DSDT analysis cannot: the illumination keys
    // (0xC4 / 0xC5) are only produced by EC0.KBLD / KBLU, and those are called
    // exclusively from the DEVS 0x00100021 branch, i.e. they fire when a
    // driver tells ACPI about the key - not when the EC sees it. So the Fn
    // brightness keys very likely never reach this method as ACPI events, but
    // that is worth confirming on the real machine:
    //
    //   ioreg -rn AsusSMC -l | grep AsusSMCKey
    //   (press Fn+F2 / Fn+F3 first)
    lastKbdKey = (uint32_t)code;
    kbdKeyCount++;
    {
        char rec[64];
        snprintf(rec, sizeof(rec), "last=0x%X count=%u", (unsigned)code,
                 (unsigned)kbdKeyCount);
        setProperty("AsusSMCKey", rec);
    }

    switch (code) {
        case 0x57: // AC disconnected
        case 0x58: // AC connected
            // ignore silently
            break;

        case 0x30: // Volume up
            dispatchCSMRReport(kHIDUsage_Csmr_VolumeIncrement);
            break;

        case 0x31: // Volume down
            dispatchCSMRReport(kHIDUsage_Csmr_VolumeDecrement);
            break;

        case 0x32: // Mute
            dispatchCSMRReport(kHIDUsage_Csmr_Mute);
            break;

        // Media buttons
        case 0x40:
        case 0x8A:
            dispatchCSMRReport(kHIDUsage_Csmr_ScanPreviousTrack);
            break;

        case 0x41:
        case 0x82:
            dispatchCSMRReport(kHIDUsage_Csmr_ScanNextTrack);
            break;

        case 0x45:
        case 0x5C:
            dispatchCSMRReport(kHIDUsage_Csmr_PlayOrPause);
            break;

        case 0x33: // hardwired On
        case 0x34: // hardwired Off
        case 0x35: // Soft Event, Fn + F7
            displayOff();
            break;

        case 0x61: // Video Mirror
            dispatchTCReport(kHIDUsage_AV_TopCase_VideoMirror);
            break;

        case 0x6B: // Fn + F9, Touchpad On/Off
            toggleTouchpad();
            break;

        case 0x5E:
            letSleep();
            break;

        case 0x7A: // Fn + A, ALS Sensor
            // We should really do this in userspace
            // (e.g. "Automatically adjust brightness")
            isALSEnabled = !isALSEnabled;
            setProperty("IsALSEnabled", isALSEnabled);
            break;

        case 0x7D: // Airplane mode
            toggleAirplaneMode();
            break;

        case 0xC6:
        case 0xC7: // ALS Notifcations
            // ignore
            break;

        case 0xC5: // Keyboard Backlight Down
            if (hasKeyboardBacklight) {
                dispatchTCReport(kHIDUsage_AV_TopCase_IlluminationDown);
            }
            break;

        case 0xC4: // Keyboard Backlight Up
            if (hasKeyboardBacklight) {
                dispatchTCReport(kHIDUsage_AV_TopCase_IlluminationUp);
            }
            break;

        default:
            if (code >= NOTIFY_BRIGHTNESS_DOWN_MIN && code<= NOTIFY_BRIGHTNESS_DOWN_MAX) // Brightness Down
                dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessDown);
            else if (code >= NOTIFY_BRIGHTNESS_UP_MIN && code<= NOTIFY_BRIGHTNESS_UP_MAX) // Brightness Up
                dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessUp);
            break;
    }

    DBGLOG("atk", "Received key %d(0x%x)", code, code);
}

void AsusSMC::letSleep() {
    kev.sendMessage(kDaemonSleep, 0, 0);
}

void AsusSMC::toggleAirplaneMode() {
    kev.sendMessage(kDaemonAirplaneMode, 0, 0);
}

void AsusSMC::toggleTouchpad() {
    dispatchMessage(kKeyboardGetTouchStatus, &isTouchpadEnabled);
    isTouchpadEnabled = !isTouchpadEnabled;
    dispatchMessage(kKeyboardSetTouchStatus, &isTouchpadEnabled);

    if (isTouchpadEnabled) {
        setProperty("IsTouchpadEnabled", true);
        DBGLOG("atk", "Enabled Touchpad");
    } else {
        setProperty("IsTouchpadEnabled", false);
        DBGLOG("atk", "Disabled Touchpad");
    }
}

void AsusSMC::toggleALS(bool state) {
    if (wmi_evaluate_method(ASUS_WMI_METHODID_DEVS, ASUS_WMI_DEVID_ALS_ENABLE, state ? 1 : 0) == -1) {
        SYSLOG("atk", "Failed to %s ALSC", state ? "enable" : "disable");
    } else {
        DBGLOG("atk", "ALS is %s", state ? "enabled" : "disabled");
    }
}

void AsusSMC::toggleBatteryConservativeMode(bool state) {
    if (!isBatteryRSOCAvailable) {
        DBGLOG("batt", "RSOC unavailable");
        return;
    }

    if (wmi_evaluate_method(ASUS_WMI_METHODID_DEVS, ASUS_WMI_DEVID_RSOC, state ? 80 : 100) != 1) {
        SYSLOG("batt", "Failed to %s battery conservative mode", state ? "enable" : "disable");
    } else {
        DBGLOG("batt", "Battery conservative mode is %s", state ? "enabled" : "disabled");
        setProperty("BatteryConservativeMode", state);
    }
}

void AsusSMC::displayOff() {
    if (isPanelBackLightOn) {
        // Read Panel brigthness value to restore later with backlight toggle
        readPanelBrightnessValue();

        dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessDown, 16);
    } else {
        dispatchTCReport(kHIDUsage_AV_TopCase_BrightnessUp, panelBrightnessLevel);
    }

    isPanelBackLightOn = !isPanelBackLightOn;
}

/**
 *  Locate the registry entry that publishes the keyboard backlight brightness.
 *
 *  Measured on the machine: the registry holds KeyboardBacklightBrightness and
 *  it tracks the System Settings slider exactly (0, 689, 1696, 3838, 4091 -
 *  the full 0..0xFFB range), while nothing ever hands that value to this
 *  keyboard's hardware. The owning entry is not documented, so it is searched
 *  for by property name and the result is published as AsusSMCBLEntry.
 */
void AsusSMC::findBrightnessEntryByProperty() {
    IORegistryIterator *iter =
        IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively);
    if (!iter) {
        setProperty("AsusSMCBLEntry", "iterator unavailable");
        return;
    }

    IOService *found = nullptr;
    while (OSObject *obj = iter->getNextObject()) {
        IOService *svc = OSDynamicCast(IOService, obj);
        if (!svc) {
            // The recursive walk starts at the plane root, which is not an
            // IOService; skip it instead of ending the walk.
            continue;
        }
        if (svc->getProperty("KeyboardBacklightBrightness")) {
            found = svc;
            break;
        }
    }
    iter->release();

    if (found) {
        found->retain();
        blEntry = found;
        setProperty("AsusSMCBLEntry", found->getName() ? found->getName() : "?");
    } else {
        setProperty("AsusSMCBLEntry", "not found");
    }
}

bool AsusSMC::readBrightnessValue(uint32_t *value) {
    if (!blEntry || !value) {
        return false;
    }

    OSObject *prop = blEntry->getProperty("KeyboardBacklightBrightness");
    if (!prop) {
        // The entry is gone; resolve it again on a later tick.
        OSSafeReleaseNULL(blEntry);
        return false;
    }

    if (OSNumber *num = OSDynamicCast(OSNumber, prop)) {
        *value = num->unsigned32BitValue();
        return true;
    }
    if (OSData *data = OSDynamicCast(OSData, prop)) {
        if (data->getLength() >= sizeof(uint32_t)) {
            *value = *reinterpret_cast<const uint32_t *>(data->getBytesNoCopy());
            return true;
        }
    }
    return false;
}

/**
 *  Drive the keyboard from the brightness value macOS publishes.
 *
 *  This is the last link the keyboard was missing: the System Settings slider
 *  moves macOS's own brightness state, and that state is readable from the
 *  registry, but nothing on this machine forwards it to the hardware. Reading
 *  it here and calling the proven HID write path closes the loop.
 *
 *  Only a change of level triggers a write, so the light is not rewritten every
 *  second, and the display-sleep lock still wins: while the panel is dark the
 *  driver owns the backlight and this does nothing.
 */
void AsusSMC::followKeyboardBrightness() {
    if (!hasKeyboardBacklight) {
        return;
    }

    // Escape hatch, so a misbehaving follow loop can be disabled without a
    // rebuild: -asussmcnofollow
    if (blFollowDisabled) {
        return;
    }

    if (!blEntry) {
        findBrightnessEntryByProperty();
    }
    if (!blEntry) {
        return;
    }

    uint32_t value = 0;
    if (!readBrightnessValue(&value)) {
        return;
    }
    blValue = value;

    // macOS uses 0..0xFFB; the hardware has 0..3.
    uint32_t level = (value * ASUS_KBD_BACKLIGHT_MAX + 2047) / 4095;
    if (level > ASUS_KBD_BACKLIGHT_MAX) {
        level = ASUS_KBD_BACKLIGHT_MAX;
    }

    char rec[128];
    snprintf(rec, sizeof(rec), "value=%u level=%u writes=%u locked=%d",
             (unsigned)value, (unsigned)level, (unsigned)blWrites,
             backlightLockedWhileDimmed ? 1 : 0);
    setProperty("AsusSMCBacklightFollow", rec);

    if (backlightLockedWhileDimmed) {
        return;
    }

    if (level == kbdLevel) {
        return;
    }

    kbdLevel = (uint8_t)level;
    blWrites++;
    int ret = sendKbdBacklightViaHID((uint8_t)level);
    SYSLOG("kbd", "followed macOS brightness: value=%u -> level=%u (ret=0x%X)",
           (unsigned)value, (unsigned)level, (unsigned)ret);
}

/**
 *  Keep the keyboard backlight in step with the panel.
 *
 *  macOS will not do this for us on this machine. Its keyboard-brightness
 *  support is per-model, and the SMC dumps show LKSB/LKSS only on 2015 and
 *  older Macs - the 2016 top-case generation moved keyboard backlight off the
 *  SMC entirely. This driver reports SMBIOS MacBookPro16,2 (2020), so macOS
 *  never touches LKSB and therefore never blanks the keyboard when the screen
 *  goes off. The result is a keyboard that stays lit through display sleep.
 *
 *  Polled from the existing sensor timer rather than hooked into power
 *  management, which keeps the change contained: we only ever call the HID
 *  write path that is already proven to work.
 */
void AsusSMC::followDisplayPower() {
    if (!hasKeyboardBacklight) {
        return;
    }

    uint32_t wranglerState = 0;
    bool haveWrangler = readDisplayWranglerState(&wranglerState);

    bool havePanel = readPanelBrightnessValue();

    // Both of the steps below need the display services to have been resolved,
    // and neither may run only once at the first timer tick.
    //
    // IODisplayWrangler is not matched that early, so on the first tick
    // wranglerEntry and panelEntry are both null. The previous revision set its
    // one-shot flags anyway, which is why every boot reported
    // AsusSMCDisplayInterest = "wrangler=0 panel=0" - the registration had
    // never actually executed, and the "off=0" result that followed said
    // nothing about whether the mechanism works.
    if (!displayInterestDone && (wranglerEntry || panelEntry)) {
        subscribeDisplayPower();
    }

    // The probe is also delayed: measuring the power chain during early boot
    // reports dictionaries that the system has not populated yet. Thirty ticks
    // is enough for IOPM to settle, and the result is then trustworthy.
    if (!powerProbeDone && ++pollTicks >= 30) {
        powerProbeDone = true;
        probePowerRegistry();
    }

    // Retry the boot-time level. It fails at start() because AsusSMC comes up
    // long before the USB keyboard enumerates (kIOReturnNotReady), and this is
    // the first place where the device is reliably available.
    if (!kbdBootApplied) {
        if (sendKbdBacklightViaHID(kbdLevel) == kIOReturnSuccess) {
            kbdBootApplied = true;
        }
    }

    // Everything is recorded rather than acted on blindly.
    //
    // The display cannot be observed from a shell while it is asleep - typing
    // to read ioreg wakes it - so the driver keeps a running record of what it
    // saw. A single read afterwards then answers whether the signal moved at
    // all, which is the only way to calibrate the semantics:
    //
    //   ioreg -rn AsusSMC -l | grep AsusSMCPanelTrace
    if (haveWrangler) {
        if (!traceSeen) {
            traceWranglerMin = traceWranglerMax = wranglerState;
            traceSeen = true;
        } else {
            if (wranglerState < traceWranglerMin) traceWranglerMin = wranglerState;
            if (wranglerState > traceWranglerMax) traceWranglerMax = wranglerState;
        }
    }
    if (havePanel) {
        if (traceBrightnessMin < 0) {
            traceBrightnessMin = traceBrightnessMax = (int)panelBrightnessLevel;
        } else {
            if ((int)panelBrightnessLevel < traceBrightnessMin) traceBrightnessMin = (int)panelBrightnessLevel;
            if ((int)panelBrightnessLevel > traceBrightnessMax) traceBrightnessMax = (int)panelBrightnessLevel;
        }
    }

    char rec[192];
    snprintf(rec, sizeof(rec),
             "wrangler=%u panelBrightness=%u dimmed=%d level=%u trace=%d,%d,%d,%d",
             (unsigned)wranglerState,
             havePanel ? (unsigned)panelBrightnessLevel : 0u,
             kbdDimmedForDisplay ? 1 : 0,
             (unsigned)kbdLevel,
             traceWranglerMin, traceWranglerMax, traceBrightnessMin, traceBrightnessMax);
    setProperty("AsusSMCPanel", rec);

    char trace[128];
    snprintf(trace, sizeof(trace), "wrangler=%d..%d brightness=%d..%d dimmed=%d",
             traceWranglerMin, traceWranglerMax,
             traceBrightnessMin, traceBrightnessMax,
             kbdDimmedForDisplay ? 1 : 0);
    setProperty("AsusSMCPanelTrace", trace);

    if (!haveWrangler && !havePanel) {
        // No display signal this tick, but the brightness follow is independent
        // of it and must not be skipped here.
        followKeyboardBrightness();
        return;
    }

    // Decide from the wrangler when it is readable, otherwise from the panel.
    //
    // Measured on the machine: with the display awake CurrentPowerState is 4,
    // and after `pmset displaysleepnow` it drops to 1 (DevicePowerState 1,
    // CapabilityFlags 0). So the "display is not fully on" states are the low
    // ordinals; anything at or above 2 is treated as awake.
    //
    // Two consecutive reads are required before blanking, so a single
    // transient value cannot turn the keyboard off.
    bool displayOff = haveWrangler ? (wranglerState <= 1)
                                   : (panelBrightnessLevel == 0);

    if (displayOff) {
        offStreak++;
    } else {
        offStreak = 0;
    }

    if (offStreak >= 2 && !kbdDimmedForDisplay) {
        int kr = sendKbdBacklightViaHID(0);
        kbdDimmedForDisplay = true;
        // From now on, macOS cannot relight the keyboard while the display is
        // dark. Released when the panel is back on.
        backlightLockedWhileDimmed = true;
        SYSLOG("kbd", "display off -> keyboard backlight off, locked (ret=0x%X)", (unsigned)kr);
    } else if (!displayOff && kbdDimmedForDisplay && offStreak == 0) {
        backlightLockedWhileDimmed = false;
        int kr = sendKbdBacklightViaHID(kbdLevel);
        kbdDimmedForDisplay = false;
        SYSLOG("kbd", "display on -> keyboard backlight %u (ret=0x%X)",
               (unsigned)kbdLevel, (unsigned)kr);
    }

    // Finally, follow whatever brightness macOS is publishing. This is what
    // makes the System Settings slider actually move the light.
    followKeyboardBrightness();
}

/**
 *  Read the current display power state.
 *
 *  Measured on this machine: CurrentPowerState is 4 with the display awake and
 *  1 after `pmset displaysleepnow`.
 *
 *  The property is not on the IODisplayWrangler service itself - reading it
 *  there always returned nothing, while `ioreg -r` shows the value because it
 *  recurses. So the service is searched first, then its children.
 */
/**
 *  Read the power state out of an IOPowerManagement property.
 *
 *  The probe answered the question that six rounds of polling could not:
 *
 *      AsusSMCPowerDiag = wPM=1/IOServicePM/0 pPM=1/IOServicePM/0
 *
 *  The property IS there, but its class is IOServicePM, not OSDictionary.
 *  Casting it to a dictionary was therefore always null, which is why every
 *  read reported nothing while ioreg happily printed the value.
 *
 *  IOServicePM serialises into exactly the dictionary ioreg displays, so that
 *  is the route used: serialise, parse the result back, then read
 *  CurrentPowerState from it.
 */
static OSDictionary *powerManagementDict(OSObject *obj) {
    if (!obj) {
        return nullptr;
    }

    // The common case: already a plain dictionary.
    if (OSDictionary *plain = OSDynamicCast(OSDictionary, obj)) {
        return plain;
    }

    // IOServicePM (and anything else opaque): go through its serialised form.
    OSSerialize *s = OSSerialize::withCapacity(4096);
    if (!s) {
        return nullptr;
    }

    OSDictionary *parsed = nullptr;
    if (obj->serialize(s) && s->getLength() > 0) {
        OSObject *result = OSUnserializeXML(s->text(), s->getLength());
        if (result) {
            parsed = OSDynamicCast(OSDictionary, result);
            if (parsed) {
                parsed->retain();
            }
            result->release();
        }
    }
    s->release();

    return parsed;
}

static bool readPowerStateFrom(IOService *svc, uint32_t *state, const char **source) {
    if (!svc) {
        return false;
    }

    OSDictionary *pm = powerManagementDict(svc->getProperty("IOPowerManagement"));
    if (!pm) {
        return false;
    }

    OSNumber *cur = OSDynamicCast(OSNumber, pm->getObject("CurrentPowerState"));
    bool ok = false;
    if (cur) {
        *state = cur->unsigned32BitValue();
        if (source) {
            *source = svc->getName();
        }
        ok = true;
    }

    pm->release();
    return ok;
}

bool AsusSMC::readDisplayWranglerState(uint32_t *state) {
    if (!state) {
        return false;
    }

    if (!wranglerEntry) {
        OSDictionary *matching = IOService::serviceMatching("IODisplayWrangler");
        if (!matching) {
            return false;
        }
        // waitForMatchingService hands back an owned reference.
        IOService *svc = IOService::waitForMatchingService(matching, 100000000ULL);
        matching->release();
        if (!svc) {
            return false;
        }
        wranglerEntry = svc;
        setProperty("AsusSMCDisplayFound", svc->getName());
    }

    const char *source = nullptr;

    // 0. The entry that worked last time.
    if (powerEntry && readPowerStateFrom(powerEntry, state, &source)) {
        return true;
    }

    // 1. The wrangler itself, then its subtree. `ioreg -r` shows the value
    //    under the wrangler, which is why it was assumed to be on the service.
    if (readPowerStateFrom(wranglerEntry, state, &source)) {
        powerEntry = wranglerEntry;
        setProperty("AsusSMCPowerSource", source ? source : "wrangler");
        return true;
    }

    if (trySubtree(wranglerEntry, 2, state, &source)) {
        setProperty("AsusSMCPowerSource", source ? source : "wrangler-child");
        return true;
    }

    // 2. The display service found for the brightness value; AppleBacklightDisplay
    //    carries the graphics properties, so it may carry the power state too.
    if (panelEntry) {
        IOService *panel = OSDynamicCast(IOService, panelEntry);
        if (readPowerStateFrom(panel, state, &source)) {
            powerEntry = panel;
            setProperty("AsusSMCPowerSource", source ? source : "panel");
            return true;
        }
        if (trySubtree(panel, 1, state, &source)) {
            setProperty("AsusSMCPowerSource", source ? source : "panel-child");
            return true;
        }
    }

    // 3. Last resort: walk the whole registry looking for a power management
    //    dictionary. The source name is reported so the choice can be judged
    //    rather than trusted, together with how many candidates were seen at
    //    all - if this reports none seen=0 the walk itself is broken, which is
    //    exactly what the previous version hid.
    {
        IORegistryIterator *iter =
            IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively);
        if (iter) {
            IOService *first = nullptr;
            const char *firstName = nullptr;
            uint32_t firstState = 0;
            IOService *displayish = nullptr;
            const char *displayishName = nullptr;
            uint32_t displayishState = 0;
            uint32_t seen = 0;

            while (OSObject *obj = iter->getNextObject()) {
                // The recursive walk starts at the plane root, which is not an
                // IOService. The previous version used the cast as the loop
                // condition, so the first such entry ended the walk and the
                // scan reported nothing at all.
                IOService *svc = OSDynamicCast(IOService, obj);
                if (!svc) {
                    continue;
                }

                uint32_t v = 0;
                const char *n = nullptr;
                if (!readPowerStateFrom(svc, &v, &n)) {
                    continue;
                }
                seen++;

                if (!first) {
                    first = svc;
                    firstName = n;
                    firstState = v;
                }

                // Prefer a display related entry over the first thing that
                // happens to answer: plenty of services carry a power
                // management dictionary.
                const char *nm = svc->getName();
                if (nm && (strstr(nm, "Display") || strstr(nm, "Wrangler"))) {
                    displayish = svc;
                    displayishName = n;
                    displayishState = v;
                    break;
                }
            }
            iter->release();

            char diag[96];
            if (displayish || first) {
                IOService *pick = displayish ? displayish : first;
                const char *pickName = displayish ? displayishName : firstName;
                uint32_t pickState = displayish ? displayishState : firstState;

                pick->retain();
                powerEntry = pick;
                *state = pickState;

                snprintf(diag, sizeof(diag), "%s state=%u seen=%u",
                         pickName ? pickName : "?", pickState, seen);
                setProperty("AsusSMCPowerSource", diag);
                return true;
            }

            snprintf(diag, sizeof(diag), "none seen=%u", seen);
            setProperty("AsusSMCPowerSource", diag);
        }
    }

    return false;
}

/**
 *  One-shot self diagnosis of the display power chain.
 *
 *  Four rounds were spent guessing which registry entry holds the display
 *  power state, and the read kept reporting nothing. This reports every hop
 *  separately so the broken link is visible in one boot instead of being
 *  inferred:
 *
 *   wPM   does IODisplayWrangler carry IOPowerManagement, of what class,
 *         and does it contain CurrentPowerState
 *   pPM   the same for the panel entry (AppleBacklightDisplay)
 *   scan  how many services the registry walk visited, how many carry
 *         IOPowerManagement at all, how many carry CurrentPowerState, and
 *         the first name that had one
 */
void AsusSMC::probePowerRegistry() {
    char diag[192];

    bool wPM = false, wCur = false, pPM = false, pCur = false;
    const char *wType = "null", *pType = "null";

    if (wranglerEntry) {
        if (OSObject *o = wranglerEntry->getProperty("IOPowerManagement")) {
            wPM = true;
            wType = o->getMetaClass()->getClassName();
            if (OSDictionary *d = powerManagementDict(o)) {
                wCur = d->getObject("CurrentPowerState") != nullptr;
                d->release();
            }
        }
    }
    if (panelEntry) {
        if (OSObject *o = panelEntry->getProperty("IOPowerManagement")) {
            pPM = true;
            pType = o->getMetaClass()->getClassName();
            if (OSDictionary *d = powerManagementDict(o)) {
                pCur = d->getObject("CurrentPowerState") != nullptr;
                d->release();
            }
        }
    }

    uint32_t scanned = 0, withPM = 0, withCur = 0;
    char firstCurName[48] = "-";

    if (IORegistryIterator *iter =
            IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively)) {
        while (OSObject *obj = iter->getNextObject()) {
            IOService *svc = OSDynamicCast(IOService, obj);
            if (!svc) {
                continue;
            }
            scanned++;

            OSObject *o = svc->getProperty("IOPowerManagement");
            if (!o) {
                continue;
            }
            withPM++;

            OSDictionary *d = powerManagementDict(o);
            if (!d || !d->getObject("CurrentPowerState")) {
                if (d) {
                    d->release();
                }
                continue;
            }
            withCur++;
            d->release();

            if (firstCurName[0] == '-') {
                const char *n = svc->getName();
                snprintf(firstCurName, sizeof(firstCurName), "%s", n ? n : "?");
            }
        }
        iter->release();
    }

    snprintf(diag, sizeof(diag),
             "wPM=%d/%s/%d pPM=%d/%s/%d scan=%u/%u/%u first=%s",
             wPM ? 1 : 0, wType, wCur ? 1 : 0,
             pPM ? 1 : 0, pType, pCur ? 1 : 0,
             scanned, withPM, withCur, firstCurName);
    setProperty("AsusSMCPowerDiag", diag);
    SYSLOG("kbd", "power registry probe: %s", diag);

    // Reading the power state out of the registry turned out to be impossible.
    // The probe reports scan=338/87/0 on this machine: 87 services carry an
    // IOPowerManagement dictionary and not one contains CurrentPowerState. The
    // value ioreg prints is produced when the registry is serialised, so a kext
    // cannot read it at all - which is why every polling attempt failed.
    //
    // registerInterestedDriver is the supported alternative: the display power
    // manager then *sends* us its transitions. No polling, no property reads.
    subscribeDisplayPower();
}

void AsusSMC::subscribeDisplayPower() {
    bool wrangler = false;
    bool panel = false;

    if (wranglerEntry) {
        wranglerEntry->registerInterestedDriver(this);
        wrangler = true;
    }
    if (panelEntry) {
        if (IOService *p = OSDynamicCast(IOService, panelEntry)) {
            p->registerInterestedDriver(this);
            panel = true;
        }
    }

    char rec[64];
    snprintf(rec, sizeof(rec), "wrangler=%d panel=%d",
             wrangler ? 1 : 0, panel ? 1 : 0);
    setProperty("AsusSMCDisplayInterest", rec);
    SYSLOG("kbd", "display power interest registered: %s", rec);

    // Only stop retrying once something actually registered. Reporting the
    // failure honestly is what makes the next result interpretable.
    if (wrangler || panel) {
        displayInterestDone = true;
    }
}

void AsusSMC::publishDisplayEvents() {
    // Counters rather than a single last-event value: the display comes back on
    // before anything can be read, so the count is the only proof that the
    // notification ever arrived.
    char rec[64];
    snprintf(rec, sizeof(rec), "off=%u on=%u",
             (unsigned)displayOffEvents, (unsigned)displayOnEvents);
    setProperty("AsusSMCDisplayEvents", rec);
}

/**
 *  Walk an entry's descendants looking for a power management dictionary.
 *  Returns the entry that answered through `source`.
 */
bool AsusSMC::trySubtree(IOService *root, int depth, uint32_t *state, const char **source) {
    if (!root || depth <= 0) {
        return false;
    }

    OSIterator *iter = root->getChildIterator(gIOServicePlane);
    if (!iter) {
        return false;
    }
    iter->reset();

    IOService *child = nullptr;
    while (OSObject *obj = iter->getNextObject()) {
        IOService *c = OSDynamicCast(IOService, obj);
        if (!c) {
            continue;
        }
        if (readPowerStateFrom(c, state, source)) {
            child = c;
            break;
        }
    }
    iter->release();

    if (child) {
        return true;
    }

    // Recurse one level down. The iterator cannot be reused after release, so
    // the children are walked again.
    OSIterator *again = root->getChildIterator(gIOServicePlane);
    if (!again) {
        return false;
    }
    again->reset();
    bool found = false;
    while (OSObject *obj = again->getNextObject()) {
        IOService *c = OSDynamicCast(IOService, obj);
        if (c && trySubtree(c, depth - 1, state, source)) {
            found = true;
            break;
        }
    }
    again->release();
    return found;
}

int AsusSMC::checkBacklightEntry() {
    if (IORegistryEntry *bkl = IORegistryEntry::fromPath(backlightEntry)) {
        OSSafeReleaseNULL(bkl);
        return 1;
    } else {
        DBGLOG("atk", "Failed to find backlight entry for %s", backlightEntry);
        return 0;
    }
}

int AsusSMC::findBacklightEntry() {
    // Check for previous found backlight entry
    if (checkBacklightEntry()) {
        return 1;
    }

    snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/IGPU@2/AppleIntelFramebuffer@0/display0/AppleBacklightDisplay");
    if (checkBacklightEntry()) {
        return 1;
    }

    snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/GFX0@2/AppleIntelFramebuffer@0/display0/AppleBacklightDisplay");
    if (checkBacklightEntry()) {
        return 1;
    }

    char deviceName[5][5] = {"PEG0", "PEGP", "PEGR", "P0P2", "IXVE"};
    for (int i = 0; i < 5; i++) {
        snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@1/IOPP/GFX0@0/NVDA,Display-A@0/NVDA/display0/AppleBacklightDisplay", deviceName[i]);
        if (checkBacklightEntry()) {
            return 1;
        }

        snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@3/IOPP/GFX0@0/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay", deviceName[i]);
        if (checkBacklightEntry()) {
            return 1;
        }

        snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@10/IOPP/GFX0@0/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay", deviceName[i]);
        if (checkBacklightEntry()) {
            return 1;
        }

        snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@1/IOPP/display@0/NVDA,Display-A@0/NVDA/display0/AppleBacklightDisplay", deviceName[i]);
        if (checkBacklightEntry()) {
            return 1;
        }

        snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@3/IOPP/display@0/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay", deviceName[i]);
        if (checkBacklightEntry()) {
            return 1;
        }

        snprintf(backlightEntry, sizeof(backlightEntry), "IOService:/AppleACPIPlatformExpert/PCI0@0/AppleACPIPCI/%s@10/IOPP/display@0/NVDA,Display-A@0/NVDATesla/display0/AppleBacklightDisplay", deviceName[i]);
        if (checkBacklightEntry()) {
            return 1;
        }
    }

    return 0;
}

bool AsusSMC::readPanelBrightnessValue() {
    if (!findBacklightEntry()) {
        DBGLOG("atk", "GPU device not found");
        // The static paths above are Intel and NVIDIA specific. On an AMD
        // machine (this GA503QM with NootedRed) they all miss, so fall back
        // to searching the registry for whichever service actually publishes
        // the display brightness.
        if (!findPanelEntryBySearch()) {
            return false;
        }
    }

    IORegistryEntry *displayDeviceEntry = IORegistryEntry::fromPath(backlightEntry);
    if (!displayDeviceEntry && panelEntry) {
        displayDeviceEntry = panelEntry;
        displayDeviceEntry->retain();
    }

    bool ok = false;
    if (displayDeviceEntry) {
        if (OSDictionary *ioDisplayParaDict = OSDynamicCast(OSDictionary, displayDeviceEntry->getProperty("IODisplayParameters"))) {
            if (OSDictionary *brightnessDict = OSDynamicCast(OSDictionary, ioDisplayParaDict->getObject("brightness"))) {
                if (OSNumber *brightnessValue = OSDynamicCast(OSNumber, brightnessDict->getObject("value"))) {
                    panelBrightnessLevel = brightnessValue->unsigned32BitValue() / 64;
                    ok = true;
                    DBGLOG("atk", "Panel brightness level: %d", panelBrightnessLevel);
                } else {
                    DBGLOG("atk", "Failed to read brightness value");
                }
            } else {
                DBGLOG("atk", "Failed to find dictionary brightness");
            }
        } else {
            DBGLOG("atk", "Failed to find dictionary IODisplayParameters");
        }
    }
    OSSafeReleaseNULL(displayDeviceEntry);
    return ok;
}

/**
 *  Find the display service that publishes IODisplayParameters.brightness.
 *
 *  Only used when the hard-coded Intel/NVIDIA paths fail, and the result is
 *  cached, so the registry walk happens at most once.
 */
bool AsusSMC::findPanelEntryBySearch() {
    if (panelEntry) {
        return true;
    }

    IORegistryIterator *iter = IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively);
    if (!iter) {
        return false;
    }

    IORegistryEntry *found = nullptr;
    while (IORegistryEntry *entry = iter->getNextObject()) {
        OSDictionary *params = OSDynamicCast(OSDictionary, entry->getProperty("IODisplayParameters"));
        if (!params) {
            continue;
        }
        OSDictionary *brightness = OSDynamicCast(OSDictionary, params->getObject("brightness"));
        if (!brightness || !brightness->getObject("value")) {
            continue;
        }
        found = entry;
        break;
    }
    iter->release();

    if (found) {
        found->retain();
        panelEntry = found;
        setProperty("AsusSMCPanelFound", found->getName());
        return true;
    }

    setProperty("AsusSMCPanelFound", "none");
    return false;
}

IOReturn AsusSMC::postKeyboardInputReport(const void *report, uint32_t reportSize) {
    IOReturn result = kIOReturnError;

    if (!report || reportSize == 0) {
        return kIOReturnBadArgument;
    }

    if (kbdDevice) {
        if (auto buffer = IOBufferMemoryDescriptor::withBytes(report, reportSize, kIODirectionNone)) {
            result = kbdDevice->handleReport(buffer, kIOHIDReportTypeInput, kIOHIDOptionsTypeNone);
            buffer->release();
        }
    }

    return result;
}

void AsusSMC::dispatchCSMRReport(int code, int loop) {
    DBGLOG("atk", "Dispatched key %d(0x%x), loop %d time(s)", code, code, loop);
    while (loop--) {
        csmrreport.keys.insert(code);
        postKeyboardInputReport(&csmrreport, sizeof(csmrreport));
        csmrreport.keys.erase(code);
        postKeyboardInputReport(&csmrreport, sizeof(csmrreport));
    }
}

void AsusSMC::dispatchTCReport(int code, int loop) {
    DBGLOG("atk", "Dispatched key %d(0x%x), loop %d time(s)", code, code, loop);
    while (loop--) {
        tcreport.keys.insert(code);
        postKeyboardInputReport(&tcreport, sizeof(tcreport));
        tcreport.keys.erase(code);
        postKeyboardInputReport(&tcreport, sizeof(tcreport));
    }
}

void AsusSMC::registerNotifications() {
    auto *key = OSSymbol::withCString(kDeliverNotifications);
    auto *propertyMatch = propertyMatching(key, kOSBooleanTrue);

    IOServiceMatchingNotificationHandler notificationHandler = OSMemberFunctionCast(IOServiceMatchingNotificationHandler, this, &AsusSMC::notificationHandler);

    _publishNotify = addMatchingNotification(gIOFirstPublishNotification,
                                             propertyMatch,
                                             notificationHandler,
                                             this,
                                             0, 10000);

    _terminateNotify = addMatchingNotification(gIOTerminatedNotification,
                                               propertyMatch,
                                               notificationHandler,
                                               this,
                                               0, 10000);

    key->release();
    propertyMatch->release();
}

void AsusSMC::notificationHandlerGated(IOService *newService, IONotifier *notifier) {
    if (notifier == _publishNotify) {
        SYSLOG("notify", "Notification consumer published: %s", newService->getName());
        _notificationServices->setObject(newService);
    }

    if (notifier == _terminateNotify) {
        SYSLOG("notify", "Notification consumer terminated: %s", newService->getName());
        _notificationServices->removeObject(newService);
    }
}

bool AsusSMC::notificationHandler(void *refCon, IOService *newService, IONotifier *notifier) {
    command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &AsusSMC::notificationHandlerGated), newService, notifier);
    return true;
}

void AsusSMC::dispatchMessageGated(int *message, void *data) {
    OSCollectionIterator *i = OSCollectionIterator::withCollection(_notificationServices);

    if (i != NULL) {
        while (IOService *service = OSDynamicCast(IOService, i->getNextObject())) {
            service->message(*message, this, data);
        }
        i->release();
    }
}

void AsusSMC::dispatchMessage(int message, void *data) {
    command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &AsusSMC::dispatchMessageGated), &message, data);
}

void AsusSMC::registerVSMC() {
    vsmcNotifier = VirtualSMCAPI::registerHandler(vsmcNotificationHandler, this);

    ALSSensor sensor {ALSSensor::Type::Unknown7, true, 6, false};
    ALSSensor noSensor {ALSSensor::Type::NoSensor, false, 0, false};
    SMCALSValue::Value emptyValue;

    // LKSB / LKSS contents.
    //
    // Neither byte is documented anywhere: upstream's defaults are {0, 1} and
    // the struct fields are literally named unknown0/unknown1, so they are a
    // guess. Tested on the machine: disabling AsusSMC makes the System
    // Settings keyboard brightness control disappear entirely, which proves
    // macOS reads these keys to decide whether to offer it. The control is
    // then shown disabled, i.e. macOS finds the key but not an adjustable
    // range.
    //
    // The working hypothesis is that the two bytes carry {current, maximum}
    // and that a maximum of 1 means "one level", leaving nothing to adjust.
    // The defaults are therefore raised to a 16 step range.
    //
    // Every byte can be overridden from boot-args, so the encoding can be
    // swept without rebuilding:
    //   -asussmclkb0=0 -asussmclkb1=16 -asussmclks0=0 -asussmclks1=16
    SMCKBrdBLightValue::lkb lkb;
    lkb.val1 = 0;
    lkb.val2 = 16;

    SMCKBrdBLightValue::lks lks;
    lks.unknown0 = 0;
    lks.unknown1 = 16;

    {
        int32_t v = 0;
        if (PE_parse_boot_argn("asussmclkb0", &v, sizeof(v))) {
            lkb.val1 = (uint8_t)v;
        }
        if (PE_parse_boot_argn("asussmclkb1", &v, sizeof(v))) {
            lkb.val2 = (uint8_t)v;
        }
        if (PE_parse_boot_argn("asussmclks0", &v, sizeof(v))) {
            lks.unknown0 = (uint8_t)v;
        }
        if (PE_parse_boot_argn("asussmclks1", &v, sizeof(v))) {
            lks.unknown1 = (uint8_t)v;
        }

        char init[96];
        snprintf(init, sizeof(init), "lkb=%u,%u lks=%u,%u",
                 lkb.val1, lkb.val2, lks.unknown0, lks.unknown1);
        setProperty("AsusSMCInit", init);
        SYSLOG("kbd", "LKSB/LKSS initial values %s", init);
    }

    // Key group switches. What was actually registered is published as
    // AsusSMCKeys, because a boot-arg that silently fails to parse is
    // indistinguishable from one that was never added:
    //
    //   -asussmcnolks   leave LKSB and LKSS unregistered
    //   -asussmcnoalv   leave the whole ambient light group unregistered
    //                   (AL, ALI0, ALI1, ALRV, ALV0, ALV1)
    bool registerLKS = !checkKernelArgument("-asussmcnolks");
    bool registerALV = !checkKernelArgument("-asussmcnoalv");

    {
        char keys[48];
        snprintf(keys, sizeof(keys), "lks=%d alv=%d",
                 registerLKS ? 1 : 0, registerALV ? 1 : 0);
        setProperty("AsusSMCKeys", keys);
    }

    // -asussmcnoalv now covers the whole ambient light group, not just ALV0 and
    // ALV1. The two-boot experiment showed the System Settings panel survives
    // both switches, but AL, ALI0, ALI1 and ALRV were registered in both boots,
    // so they were never actually separated from anything:
    //
    //   boot A: lks=0 alv=1  -> panel present (ALV0/1 + AL/ALI0/ALI1/ALRV)
    //   boot B: lks=1 alv=0  -> panel present (AL/ALI0/ALI1/ALRV still on)
    //
    // ALI0 and ALI1 are the sensor descriptors: real MacBookPro11,4 reports
    // ALI0 = 07000600, which is exactly what this driver's
    // {Unknown7, true, 6, false} produces, so the panel is almost certainly
    // driven by the presence of these keys.
    if (registerALV) {
        VirtualSMCAPI::addKey(KeyAL, vsmcPlugin.data, VirtualSMCAPI::valueWithUint16(
            0, &forceBits,
            SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE));

        VirtualSMCAPI::addKey(KeyALI0, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
            reinterpret_cast<const SMC_DATA *>(&sensor), sizeof(sensor), SmcKeyTypeAli, nullptr,
            SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_FUNCTION));

        VirtualSMCAPI::addKey(KeyALI1, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
            reinterpret_cast<const SMC_DATA *>(&noSensor), sizeof(noSensor), SmcKeyTypeAli, nullptr,
            SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_FUNCTION));

        VirtualSMCAPI::addKey(KeyALRV, vsmcPlugin.data, VirtualSMCAPI::valueWithUint16(
            1, nullptr,
            SMC_KEY_ATTRIBUTE_READ));
    }

    if (registerALV) {
        VirtualSMCAPI::addKey(KeyALV0, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
            reinterpret_cast<const SMC_DATA *>(&emptyValue), sizeof(emptyValue), SmcKeyTypeAlv, new SMCALSValue(&currentLux, &forceBits),
            SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

        VirtualSMCAPI::addKey(KeyALV1, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
            reinterpret_cast<const SMC_DATA *>(&emptyValue), sizeof(emptyValue), SmcKeyTypeAlv, nullptr,
            SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));
    }

    // LKSB / LKSS are registered read/write, not write-only.
    //
    // A previous revision made them write-only because the real Mac dumps
    // report "NOT READABLE, code 85" and the attribute byte there is 0x50
    // (WRITE | FUNCTION). That was a mistake, for two reasons:
    //
    //   * The System Settings keyboard brightness control is still displayed
    //     on this machine, greyed out. A machine with no keyboard backlight at
    //     all does not show that control, so macOS is detecting a backlight -
    //     and LKSB/LKSS are the only such interface this driver publishes. So
    //     macOS is looking at these keys after all.
    //   * If macOS reads the current level to render the slider, a key that
    //     cannot be read is exactly what would leave the slider disabled.
    //     "NOT READABLE" in the dump may also just be the dump tool lacking
    //     the access those keys want.
    //
    // Keeping READ also restores the read counters, which are the direct
    // evidence of whether macOS touches these keys at all.
    if (registerLKS) {
        VirtualSMCAPI::addKey(KeyLKSB, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
            reinterpret_cast<const SMC_DATA *>(&lkb), sizeof(lkb), SmcKeyTypeLkb, new SMCKBrdBLightValue(this),
            SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

        VirtualSMCAPI::addKey(KeyLKSS, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
            reinterpret_cast<const SMC_DATA *>(&lks), sizeof(lks), SmcKeyTypeLks, new SMCKBrdBLightState(this),
            SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));
    }

    VirtualSMCAPI::addKey(KeyMSLD, vsmcPlugin.data, VirtualSMCAPI::valueWithUint8(
        0, nullptr,
        SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_FUNCTION));

    if (isBatteryRSOCAvailable) {
        VirtualSMCAPI::addKey(KeyBDVT, vsmcPlugin.data, VirtualSMCAPI::valueWithFlag(
            false, new BDVT(this),
            SMC_KEY_ATTRIBUTE_READ | SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_ATOMIC));
    }

    qsort(const_cast<VirtualSMCKeyValue *>(vsmcPlugin.data.data()), vsmcPlugin.data.size(), sizeof(VirtualSMCKeyValue), VirtualSMCKeyValue::compare);
}

bool AsusSMC::vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier) {
    if (sensors && vsmc) {
        DBGLOG("atk", "got vsmc notification");
        auto self = static_cast<AsusSMC *>(sensors);
        auto ret = vsmc->callPlatformFunction(VirtualSMCAPI::SubmitPlugin, true, sensors, &self->vsmcPlugin, nullptr, nullptr);
        if (ret == kIOReturnSuccess) {
            DBGLOG("atk", "Submitted plugin");

            self->workloop = self->getWorkLoop();
            self->poller = IOTimerEventSource::timerEventSource(self, [](OSObject *object, IOTimerEventSource *sender) {
                auto ls = OSDynamicCast(AsusSMC, object);
                if (ls) {
                    ls->refreshALS(true);
                    // Fan refresh lives in AsusFanControl.kext now; it runs
                    // its own timer and its own ACPI access.
                    ls->followDisplayPower();
                    // Reports whether the ATKD event path answers at all.
                    ls->probeATKEventPath();
                }
            });

            if (!self->poller || !self->workloop) {
                SYSLOG("atk", "Failed to create poller or workloop");
                return false;
            }

            if (self->workloop->addEventSource(self->poller) != kIOReturnSuccess) {
                SYSLOG("atk", "Failed to add timer event source to workloop");
                return false;
            }

            if (self->poller->setTimeoutMS(SensorUpdateTimeoutMS) != kIOReturnSuccess) {
                SYSLOG("atk", "Failed to set timeout");
                return false;
            }

            return true;
        } else if (ret != kIOReturnUnsupported) {
            SYSLOG("atk", "Plugin submission failure %X", ret);
        } else {
            DBGLOG("atk", "Plugin submission to non vsmc");
        }
    } else {
        SYSLOG("atk", "Got null vsmc notification");
    }

    return false;
}

EXPORT extern "C" kern_return_t ADDPR(kern_start)(kmod_info_t *, void *) {
    // Report success but actually do not start and let I/O Kit unload us.
    // This works better and increases boot speed in some cases.
    PE_parse_boot_argn("liludelay", &ADDPR(debugPrintDelay), sizeof(ADDPR(debugPrintDelay)));
    ADDPR(debugEnabled) = checkKernelArgument("-vsmcdbg") || checkKernelArgument("-asussmcdbg");

    // Unconditional banner. This is a SYSLOG, not a DBGLOG, so it prints
    // even when debugging is off. Without it there is no way to tell from the
    // log which build actually got loaded, and no way to tell whether a
    // missing DBGLOG means "debug is off" or "the code never ran".
    SYSLOG("atk", "kern_start: debugEnabled=%d", ADDPR(debugEnabled) ? 1 : 0);

    if (!ADDPR(debugEnabled)) {
        SYSLOG("atk", "DBG output is OFF; add -asussmcdbg (or -vsmcdbg) to boot-args");
    }

    return KERN_SUCCESS;
}


EXPORT extern "C" kern_return_t ADDPR(kern_stop)(kmod_info_t *, void *) {
    // It is not safe to unload VirtualSMC plugins!
    return KERN_FAILURE;
}
