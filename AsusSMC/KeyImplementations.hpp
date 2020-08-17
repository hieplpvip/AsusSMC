//
//  KeyImplementations.hpp
//  AsusSMC
//
//  Copyright © 2018-2020 Le Bao Hiep. All rights reserved.
//

#ifndef KeyImplementations_hpp
#define KeyImplementations_hpp

#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <VirtualSMCSDK/kern_vsmcapi.hpp>
#include "AsusHIDDriver.hpp"

/**
 *  Key name definitions for VirtualSMC
 */
static constexpr SMC_KEY KeyAL   = SMC_MAKE_IDENTIFIER('A','L','!',' ');
static constexpr SMC_KEY KeyALI0 = SMC_MAKE_IDENTIFIER('A','L','I','0');
static constexpr SMC_KEY KeyALI1 = SMC_MAKE_IDENTIFIER('A','L','I','1');
static constexpr SMC_KEY KeyALRV = SMC_MAKE_IDENTIFIER('A','L','R','V');
static constexpr SMC_KEY KeyALV0 = SMC_MAKE_IDENTIFIER('A','L','V','0');
static constexpr SMC_KEY KeyALV1 = SMC_MAKE_IDENTIFIER('A','L','V','1');
static constexpr SMC_KEY KeyLKSB = SMC_MAKE_IDENTIFIER('L','K','S','B');
static constexpr SMC_KEY KeyLKSS = SMC_MAKE_IDENTIFIER('L','K','S','S');
static constexpr SMC_KEY KeyMSLD = SMC_MAKE_IDENTIFIER('M','S','L','D');

class ALSForceBits : public VirtualSMCValue {
public:
    /**
     *  Each "1" bit in gui8ALSForced indicates that a certain writable ALS
     *  variable has been overridden (i.e., forced) by the host OS or
     *  host diagnostics, and that variable should not be written by the SMC
     *  again until the applicable bit is cleared in gui8ALSForced.
     *  Currently, the used bits are:
     *      Bit 0 protects gui16ALSScale
     *      Bit 1 protects ui16Chan0 and ui16Chan1 of aalsvALSData
     *      Bit 2 protects gui16ALSLux
     *      Bit 3 protects fHighGain of aalsvALSData
     *      Bit 4 protects gai16ALSTemp[MAX_ALS_SENSORS]
     *  All other bits are reserved and should be cleared to 0.
     */
    enum {
        kALSForceScale      = 1,
        kALSForceChan       = 2,
        kALSForceLux        = 4,
        kALSForceHighGain   = 8,
        kALSForceTemp       = 16
    };

    uint8_t bits() { return data[0]; }
};

/**
 *  ALSSensor structure contains sensor-specific information for this system
 */
struct ALSSensor {
    /**
     *  Supported sensor types
     */
    enum Type : uint8_t {
        NoSensor  = 0,
        BS520     = 1,
        TSL2561CS = 2,
        LX1973A   = 3,
        ISL29003  = 4,
        Unknown7  = 7
    };

    /**
     *  Sensor type
     */
    Type sensorType {Type::NoSensor};

    /**
     * TRUE if no lid or if sensor works with closed lid.
     * FALSE otherwise.
     */
    bool validWhenLidClosed {false};

    /**
     *  Possibly ID
     */
    uint8_t unknown {0};

    /**
     * TRUE if the SIL brightness depends on this sensor's value.
     * FALSE otherwise.
     */
    bool controlSIL {false};

    ALSSensor(Type sensorType, bool validWhenLidClosed, uint8_t unknown, bool controlSIL): sensorType(sensorType), validWhenLidClosed(validWhenLidClosed), unknown(unknown), controlSIL(controlSIL) {}
};

class SMCALSValue : public VirtualSMCValue {
    _Atomic(uint32_t) *currentLux;
    ALSForceBits *forceBits;

protected:
    SMC_RESULT readAccess() override;

public:
    /**
     *  Contains latest ambient light info from 1 sensor
     */
    struct PACKED Value {
        /**
         *  If TRUE, data in this struct is valid.
         */
        bool valid {false};

        /**
         *  If TRUE, ui16Chan0/1 are high-gain readings.
         *  If FALSE, ui16Chan0/1 are low-gain readings.
         */
        bool highGain {true};

        /**
         *  I2C channel 0 data or analog(ADC) data.
         */
        uint16_t chan0 {0};

        /**
         *  I2C channel 1 data.
         */
        uint16_t chan1 {0};

        /**
         * The following field only exists on systems that send ALS change notifications to the OS:
         * Room illumination in lux, FP18.14.
         */
        uint32_t roomLux {0};
    };

    SMCALSValue(_Atomic(uint32_t) *currentLux, ALSForceBits *forceBits) :
    currentLux(currentLux), forceBits(forceBits) {}
};

class SMCKBrdBLightValue : public VirtualSMCValue {
protected:
    IOACPIPlatformDevice *atkDevice {nullptr};
    OSSet *_hidDrivers {nullptr};

public:
    /**
     *  Keyboard backlight brightness
     */
    struct PACKED lks {
        uint8_t unknown0 {0};
        uint8_t unknown1 {1};
    };
    struct PACKED lkb {
        uint8_t val1 {0};
        uint8_t val2 {1};
    };

    SMCKBrdBLightValue(IOACPIPlatformDevice *atkDevice, OSSet *_hidDrivers): atkDevice(atkDevice), _hidDrivers(_hidDrivers) {}

    SMC_RESULT update(const SMC_DATA *src) override;
};

#endif /* KeyImplementations_hpp */
