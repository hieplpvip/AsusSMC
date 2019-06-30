/*
 * @APPLE_LICENSE_HEADER_START@
 *
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */
#ifndef _APPLEHIDUSAGETABLES_H
#define _APPLEHIDUSAGETABLES_H

/* ******************************************************************************************
 * Apple HID Usage Tables
 *
 * The following constants are Apple Vendor specific usages
 * ****************************************************************************************** */


/* Usage Pages */
enum
{
    kHIDPage_AppleVendor                        = 0xff00,
    kHIDPage_AppleVendorKeyboard                = 0xff01,
    kHIDPage_AppleVendorMouse                   = 0xff02,
    kHIDPage_AppleVendorAccelerometer           = 0xff03,
    kHIDPage_AppleVendorAmbientLightSensor      = 0xff04,
    kHIDPage_AppleVendorTemperatureSensor       = 0xff05,
    kHIDPage_AppleVendorHeadset                 = 0xff07,
    kHIDPage_AppleVendorPowerSensor             = 0xff08,
    kHIDPage_AppleVendorSmartCover              = 0xff09,
    kHIDPage_AppleVendorPlatinum                = 0xff0A,
    kHIDPage_AppleVendorLisa                    = 0xff0B,
    kHIDPage_AppleVendorFilteredEvent           = 0xff50,
    kHIDPage_AppleVendorDisplay                 = 0xff92,
    kHIDPage_AppleVendorTopCase                 = 0x00ff
};


/* AppleVendor Page (0xff00) */
enum
{
    kHIDUsage_AppleVendor_TopCase               = 0x0001, /* Application Collection */
    kHIDUsage_AppleVendor_Display               = 0x0002, /* Application Collection */
    kHIDUsage_AppleVendor_Accelerometer         = 0x0003, /* Application Collection */
    kHIDUsage_AppleVendor_AmbientLightSensor    = 0x0004, /* Application Collection */
    kHIDUsage_AppleVendor_TemperatureSensor     = 0x0005, /* Application Collection */
    kHIDUsage_AppleVendor_Keyboard              = 0x0006, /* Application Collection */
    kHIDUsage_AppleVendor_Headset               = 0x0007, /* Application Collection */
    kHIDUsage_AppleVendor_ProximitySensor       = 0x0008, /* Application Collection */
    kHIDUsage_AppleVendor_Gyro                  = 0x0009, /* Application Collection */
    kHIDUsage_AppleVendor_Compass               = 0x000A, /* Application Collection */
    kHIDUsage_AppleVendor_DeviceManagement      = 0x000B, /* Application Collection */
    kHIDUsage_AppleVendor_Trackpad              = 0x000C, /* Application Collection */
    kHIDUsage_AppleVendor_TopCaseReserved       = 0x000D, /* Application Collection */
    kHIDUsage_AppleVendor_Motion                = 0x000E, /* Application Collection */
    kHIDUsage_AppleVendor_KeyboardBacklight     = 0x000F, /* Application Collection */
};


/* AppleVendor Keyboard Page (0xff01) */
enum
{
    kHIDUsage_AppleVendorKeyboard_Spotlight             = 0x0001,
    kHIDUsage_AppleVendorKeyboard_Dashboard             = 0x0002,
    kHIDUsage_AppleVendorKeyboard_Function              = 0x0003,
    kHIDUsage_AppleVendorKeyboard_Launchpad             = 0x0004,
    kHIDUsage_AppleVendorKeyboard_Reserved              = 0x000a,
    kHIDUsage_AppleVendorKeyboard_CapsLockDelayEnable   = 0x000b,
    kHIDUsage_AppleVendorKeyboard_PowerState            = 0x000c,
    kHIDUsage_AppleVendorKeyboard_Expose_All            = 0x0010,
    kHIDUsage_AppleVendorKeyboard_Expose_Desktop        = 0x0011,
    kHIDUsage_AppleVendorKeyboard_Brightness_Up         = 0x0020,
    kHIDUsage_AppleVendorKeyboard_Brightness_Down       = 0x0021
};

/* AppleVendor Page Headset (0xff07) */
enum
{
    kHIDUsage_AV_Headset_Availability           = 0x0001
};

/* AppleVendor Power Page (0xff08) */
enum {
    kHIDUsage_AppleVendorPowerSensor_Power      = 0x0001,
    kHIDUsage_AppleVendorPowerSensor_Current    = 0x0002,
    kHIDUsage_AppleVendorPowerSensor_Voltage    = 0x0003,
};

/* AppleVendor Smart Cover Page (0xff09) */
enum {
    kHIDUsage_AppleVendorSmartCover_Open        = 0x0001,
    kHIDUsage_AppleVendorSmartCover_Flap1       = 0x0002,
    kHIDUsage_AppleVendorSmartCover_Flap2       = 0x0003,
    kHIDUsage_AppleVendorSmartCover_Flap3       = 0x0004,
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


#endif /* _APPLEHIDUSAGETABLES_H */
