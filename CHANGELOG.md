AsusSMC Changelog
=======================

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
