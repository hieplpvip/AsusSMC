//
//  HIDUsageTables.h
//  AsusSMC
//
//  Copyright © 2019 Le Bao Hiep. All rights reserved.
//

#ifndef _HIDUsageTables_h
#define _HIDUsageTables_h

/* Usage Pages */
enum {
    kHIDPage_AppleVendorTopCase = 0x00ff,
    kHIDPage_AsusVendor         = 0xff31,
    kHIDPage_MicrosoftVendor    = 0xff00
};

/* AppleVendor Page Top Case (0x00ff) */
enum
{
    kHIDUsage_AV_TopCase_KeyboardFn            = 0x0003,
    kHIDUsage_AV_TopCase_BrightnessUp          = 0x0004,
    kHIDUsage_AV_TopCase_BrightnessDown        = 0x0005,
    kHIDUsage_AV_TopCase_VideoMirror           = 0x0006,
    kHIDUsage_AV_TopCase_IlluminationToggle    = 0x0007,
    kHIDUsage_AV_TopCase_IlluminationUp        = 0x0008,
    kHIDUsage_AV_TopCase_IlluminationDown      = 0x0009,
    kHIDUsage_AV_TopCase_ClamshellLatched      = 0x000a,
    kHIDUsage_AV_TopCase_Reserved_MouseData    = 0x00c0
};

/* AsusVendor Page (0xff31) */
enum {
    kHIDUsage_AsusVendor_BrightnessDown      = 0x10,
    kHIDUsage_AsusVendor_BrightnessUp        = 0x20,
    kHIDUsage_AsusVendor_DisplayOff          = 0x35,
    kHIDUsage_AsusVendor_ROG                 = 0x38,
    kHIDUsage_AsusVendor_Power4Gear          = 0x5c, /* Fn+Space Power4Gear Hybrid */
    kHIDUsage_AsusVendor_TouchpadToggle      = 0x6b,
    kHIDUsage_AsusVendor_Sleep               = 0x6c,
    kHIDUsage_AsusVendor_MicMute             = 0x7c,
    kHIDUsage_AsusVendor_Camera              = 0x82,
    kHIDUsage_AsusVendor_RFKill              = 0x88,
    kHIDUsage_AsusVendor_Fan                 = 0x99, /* Fn+F5 "fan" symbol on FX503VD */
    kHIDUsage_AsusVendor_Calc                = 0xb5,
    kHIDUsage_AsusVendor_Splendid            = 0xba, /* Fn+C ASUS Splendid */
    kHIDUsage_AsusVendor_IlluminationUp      = 0xc4,
    kHIDUsage_AsusVendor_IlluminationDown    = 0xc5
};

/* MicrosoftVendor Page (0xff31) */
enum {
    kHIDUsage_MicrosoftVendor_WLAN              = 0xf1,
    kHIDUsage_MicrosoftVendor_BrightnessDown    = 0xf2,
    kHIDUsage_MicrosoftVendor_BrightnessUp      = 0xf3,
    kHIDUsage_MicrosoftVendor_DisplayOff        = 0xf4,
    kHIDUsage_MicrosoftVendor_Camera            = 0xf7,
    kHIDUsage_MicrosoftVendor_ROG               = 0xf8,
};

#endif /* _HIDUsageTables_h */
