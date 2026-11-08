//
//  KeyImplementations.hpp
//  AsusSMC
//
//  Copyright 漏 2018-2020 Le Bao Hiep. All rights reserved.
//
//  Ambient light sensor support is based on SMCLightSensor

#ifndef KeyImplementations_hpp
#define KeyImplementations_hpp

#include <IOKit/IOService.h>
#include <VirtualSMCSDK/kern_vsmcapi.hpp>

#define kSetKeyboardBacklightMessage 812002

static constexpr SMC_KEY KeyAL   = SMC_MAKE_IDENTIFIER('A','L','!',' ');
static constexpr SMC_KEY KeyALI0 = SMC_MAKE_IDENTIFIER('A','L','I','0');
static constexpr SMC_KEY KeyALI1 = SMC_MAKE_IDENTIFIER('A','L','I','1');
static constexpr SMC_KEY KeyALRV = SMC_MAKE_IDENTIFIER('A','L','R','V');
static constexpr SMC_KEY KeyALV0 = SMC_MAKE_IDENTIFIER('A','L','V','0');
static constexpr SMC_KEY KeyALV1 = SMC_MAKE_IDENTIFIER('A','L','V','1');
static constexpr SMC_KEY KeyLKSB = SMC_MAKE_IDENTIFIER('L','K','S','B');
static constexpr SMC_KEY KeyLKSS = SMC_MAKE_IDENTIFIER('L','K','S','S');
static constexpr SMC_KEY KeyMSLD = SMC_MAKE_IDENTIFIER('M','S','L','D');

static constexpr SMC_KEY KeyBDVT = SMC_MAKE_IDENTIFIER('B','D','V','T');

class ALSForceBits : public VirtualSMCValue {
public:
    enum {
        kALSForceScale      = 1,
        kALSForceChan       = 2,
        kALSForceLux        = 4,
        kALSForceHighGain   = 8,
        kALSForceTemp       = 16
    };

    uint8_t bits() { return data[0]; }
};

struct ALSSensor {
    enum Type : uint8_t {
        NoSensor  = 0,
        BS520     = 1,
        TSL2561CS = 2,
        LX1973A   = 3,
        ISL29003  = 4,
        Unknown7  = 7
    };

    Type sensorType {Type::NoSensor};
    bool validWhenLidClosed {false};
    uint8_t unknown {0};
    bool controlSIL {false};

    ALSSensor(Type sensorType, bool validWhenLidClosed, uint8_t unknown, bool controlSIL): sensorType(sensorType), validWhenLidClosed(validWhenLidClosed), unknown(unknown), controlSIL(controlSIL) {}
};

class SMCALSValue : public VirtualSMCValue {
    _Atomic(uint32_t) *currentLux;
    ALSForceBits *forceBits;

protected:
    SMC_RESULT readAccess() override;

public:
    struct PACKED Value {
        bool valid {false};
        bool highGain {true};
        uint16_t chan0 {0};
        uint16_t chan1 {0};
        uint32_t roomLux {0};
    };

    SMCALSValue(_Atomic(uint32_t) *currentLux, ALSForceBits *forceBits) :
    currentLux(currentLux), forceBits(forceBits) {}
};

class SMCKBrdBLightValue : public VirtualSMCValue {
    IOService *asusSMCInstance {nullptr};

protected:
    SMC_RESULT update(const SMC_DATA *src) override;
    // Reads are tracked as well: if macOS never writes LKSB, the question
    // becomes whether it even looks at these keys.
    SMC_RESULT readAccess() override;

public:
    struct PACKED lks {
        uint8_t unknown0 {0};
        uint8_t unknown1 {1};
    };
    struct PACKED lkb {
        uint8_t val1 {0};
        uint8_t val2 {1};
    };

    SMCKBrdBLightValue(IOService *asusSMCInstance): asusSMCInstance(asusSMCInstance) {}
};

/**
 *  LKSS, the keyboard backlight state/capability key.
 *
 *  macOS reads this to decide whether the keyboard brightness control is
 *  usable at all, so it is instrumented separately from LKSB.
 */
class SMCKBrdBLightState : public VirtualSMCValue {
    IOService *asusSMCInstance {nullptr};

protected:
    SMC_RESULT readAccess() override;
    // Writes are counted as well: if macOS drives the keyboard backlight
    // through LKSS instead of LKSB, this is where it would show up.
    SMC_RESULT update(const SMC_DATA *src) override;

public:
    SMCKBrdBLightState(IOService *asusSMCInstance): asusSMCInstance(asusSMCInstance) {}
};

class BDVT : public VirtualSMCValue {
    IOService *dst {nullptr};

protected:
    SMC_RESULT update(const SMC_DATA *src) override;

public:
    BDVT(IOService *dst) : dst(dst) {}
};

#endif /* KeyImplementations_hpp */
