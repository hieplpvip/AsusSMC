# AsusSMC

A VirtualSMC plugin provides native support for ALS, keyboard backlight and Fn keys on macOS.

#### Features
- Full Fn keys support
- Native ALS and keyboard backlight support

#### Requirements
- Asus laptop with ATK device
- To get keyboard backlight working, you need an ALS sensor (can be fake)

#### Boot arguments
- Add `-asussmcdbg` to enable debug printing (available in DEBUG binaries).

#### How to install
- Patch DSDT (instruction coming)
- Install this kext together with VirtualSMC and Lilu

#### Credits
- [Apple](https://www.apple.com) for macOS
- [vit9696](https://github.com/vit9696) for [Lilu](https://github.com/acidanthera/Lilu) and [VirtualSMC](https://github.com/acidanthera/VirtualSMC)
- [lvs1974](https://github.com/lvs1974) and [usr-sse2](https://github.com/usr-sse2) for developing laptop sensor support
- [EMlyDinEsHMG](https://osxlatitude.com/profile/7370-emlydinesh/) for [AsusNBFnKeys source code](https://github.com/EMlyDinEsHMG/AsusNBFnKeys)
