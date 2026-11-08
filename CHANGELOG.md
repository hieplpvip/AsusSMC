AsusSMC Changelog
=======================

#### v1.4.2
- **Fix the WMI argument buffer size.** `wmi_args` was 8 bytes while the DSDT's
  `ATKD.WMNB` declares 20, so every WMI call was rejected with
  `AE_AML_BUFFER_LIMIT` on boards of this generation. Fan telemetry, thermal
  profile and every other WMI-backed key were dead as a result. The struct is
  now 24 bytes (six `uint32` fields), which matches the declaration.
- **Fall back to WMI when `TACH` is unusable.** On the GA503QM, `EC0.TACH()`
  computes `0x0041CDB4 / (raw * 2)`, which with the counters the EC actually
  reports yields 59000-93750 "RPM". The firmware path
  (`DEVS`/`DSTS` device `0x00110013`/`0x00110014`) reports the same counter
  decoded as `raw * 100`, matching `asus-wmi.c`, and `TACH` is only used when
  it returns something plausible.
- **Blank the keyboard backlight on display sleep.** macOS does not do this
  here: `LKSB`/`LKSS` only exist on 2015-and-earlier Macs, and this machine
  reports a 2020 SMBIOS, so macOS never touches them. The driver now follows
  the display power state itself.
  Note for anyone reading the registry: `IOPowerManagement` is an
  `IOServicePM`, not an `OSDictionary`; a `OSDynamicCast` to a dictionary is
  always null even though `ioreg -r` prints the value, because ioreg goes
  through the serialisation path. `CurrentPowerState` is 4 awake and 1 asleep.
- **Make the System Settings keyboard brightness slider work.** Two independent
  causes had to be removed. First, automatic keyboard brightness disables the
  manual slider, and it is enabled here because this driver publishes ambient
  light keys; `cbprobe auto-off` (or turning the setting off) unlocks it. Second,
  macOS then only updates its own brightness state and never sends it to the
  hardware, so the driver reads `KeyboardBacklightBrightness` from the registry
  once a second and drives the light itself. That value tracks the slider over
  the full `0..0xFFB` range and lives on `AppleLMUController` - do not remove
  the ALS keys, they are what brings that service into existence.
- **Split fan control into a separate `AsusFanControl.kext`.** Fan data comes
  from the ASUS platform firmware over the same WMI/ATK device the other
  features use, so it does not belong in the SMC layer. The fan SMC keys moved
  with it: a virtual key can only be registered once, so they cannot be shared
  between two kexts. Both kexts match the ATKD device independently.
- Both kexts are built by Xcode rather than by a hand-written link. A kext
  target runs `CreateKModInfo`, and the resulting `kmod_info` object is what the
  kernel requires in order to load the extension at all.

#### v1.4.1
- Added MacKernelSDK with Xcode 12 compatibility
- Drop experimental custom fan control code
- Drop AsusHID as I couldn't make it work on Catalina and above. Please use ROG-HID by @black-dragon74
- Improving touchpad toggling mechanism

#### v1.4.0
- Rework keyboard backlight mechanism. Users need to repatch DSDT. More specific, the line `^^KBLV = Arg0 / 16` must be removed.
- Rework ambient light sensor mechanism (use WMI for toggling sensor; read value directly with method `_ALI`). DSDT patch is not needed anymore.
- Add experimental support for reading fan speed with WMI
- Support battery charge threshold found on new laptops (charging stops at 80%). Can be disabled by turning off `Battery Health` in `Energy Saver`.

#### v1.3.1
- Removed unimplemented virtual methods for kext to load correctly (thanks @Ubsefor)

#### v1.3.0
- Native keyboard backlight on Catalina

#### v1.2.3
- Remove unused WMI code
- Correct usage of `powerStateOrdinal` in `setPowerState`

#### v1.2.2
- Fix keyboard backlight remaining off after sleep on Catalina (thanks to @AR-CADE)

#### v1.2.1
- Built with Lilu 1.4.1 and VirtualSMC 1.1.0
- Update `OSBundleLibraries`
- Fix `install_daemon.sh` to make it work when folder `/usr/local/bin` does not exist

#### v1.2.0
- Added support for USB HID keyboards found on ROG models
- Optimized code
- Updated MaciASL patches
- Unified release archive names
- Changed dependency com.apple.iokit.IOHIDSystem to com.apple.iokit.IOHIDFamily
- Change AsusSMCDaemon installation location to support Catalina
- Add workaround for keyboard backlight on Catalina

#### v1.1.1
- Fixed memory leaks
- Added support for direct messages from ACPI

#### v1.1
- Optimized code

#### v1.0.3
- Improved message handling mechanism

#### v1.0.2
- Optimized code

#### v1.0.1
- Write data to SMC key after setting keyboard backlight

#### v1.0.0
- Initial release
