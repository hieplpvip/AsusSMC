# AsusSMC

[![Github release](https://img.shields.io/github/release/hieplpvip/AsusSMC.svg?color=blue)](https://github.com/hieplpvip/AsusSMC/releases/latest)
[![Github downloads](https://img.shields.io/github/downloads/hieplpvip/AsusSMC/total.svg?color=blue)](https://github.com/hieplpvip/AsusSMC/releases)
[![Build Status](https://travis-ci.com/hieplpvip/AsusSMC.svg?branch=master)](https://travis-ci.com/hieplpvip/AsusSMC)
[![Scan Status](https://scan.coverity.com/projects/18304/badge.svg)](https://scan.coverity.com/projects/18304)
[![Gitter chat](https://img.shields.io/gitter/room/nwjs/nw.js.svg?colorB=ed1965)](https://gitter.im/hieplpvip/AsusSMC)
[![Donate with PayPal](https://img.shields.io/badge/paypal-donate-red.svg)](https://paypal.me/lebhiep)

A VirtualSMC plugin provides native support for ALS, keyboard backlight and Fn keys for Asus laptops on macOS.

#### Features
- Full Fn keys support
- Native ALS support
- Native keyboard backlight support (16 levels, smooth transition, auto adjusting, auto turning off)
- Battery Health Charging

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
- [tekezo](https://github.com/tekezo) for [VirtualHIDKeyboard](https://github.com/pqrs-org/Karabiner-VirtualHIDDevice/)
- [williambj1](https://github.com/williambj1) for testing HID support
