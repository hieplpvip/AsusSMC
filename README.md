# AsusSMC

[![Scan Status](https://scan.coverity.com/projects/18304/badge.svg?flat=1)](https://scan.coverity.com/projects/18304)

A VirtualSMC plugin provides native support for ALS, keyboard backlight and Fn keys for Asus laptops on macOS.

#### Features
- Almost full Fn keys support including sleep, airplane mode, trackpad (only with VoodooI2C). Media keys is not supported now, sorry :(
- Native ALS support
- Native keyboard backlight support (16 levels, smooth transition, auto adjusting, auto turning off)

#### Requirements
- Asus laptop with ATK device
- Knowing how to patch DSDT (if not, read [this](https://www.tonymacx86.com/threads/guide-patching-laptop-dsdt-ssdts.152573/))

#### Boot arguments
- Add `-asussmcdbg` to enable debug printing (available in DEBUG binaries).

#### How to install
- Instruction is available in the Wiki.

#### Credits
- [Apple](https://www.apple.com) for macOS
- [vit9696](https://github.com/vit9696) for [Lilu](https://github.com/acidanthera/Lilu) and [VirtualSMC](https://github.com/acidanthera/VirtualSMC)
- [lvs1974](https://github.com/lvs1974) and [usr-sse2](https://github.com/usr-sse2) for developing ambient light sensor support
- [EMlyDinEsHMG](https://osxlatitude.com/profile/7370-emlydinesh/) for [AsusNBFnKeys source code](https://github.com/EMlyDinEsHMG/AsusNBFnKeys)
- [Objective-See](https://objective-see.com) for [kernel-userspace communication](https://objective-see.com/blog/blog_0x0B.html)
- [Karabiner-Elements](https://github.com/tekezo/Karabiner-Elements) for VirtualHIDKeyboard
