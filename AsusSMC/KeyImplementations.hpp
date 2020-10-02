//
//  KeyImplementations.hpp
//  AsusSMC
//
//  Copyright © 2018-2020 Le Bao Hiep. All rights reserved.
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

static constexpr SMC_KEY KeyFNum = SMC_MAKE_IDENTIFIER('F','N','u','m');
static constexpr SMC_KEY KeyF0ID = SMC_MAKE_IDENTIFIER('F','0','I','D');
static constexpr SMC_KEY KeyF0Ac = SMC_MAKE_IDENTIFIER('F','0','A','c');

static constexpr SMC_KEY KeyBDVT = SMC_MAKE_IDENTIFIER('B','D','V','T');

typedef enum { FAN_PWM_TACH, FAN_RPM, PUMP_PWM, PUMP_RPM, FAN_PWM_NOTACH, EMPTY_PLACEHOLDER } FanType;

typedef enum {
    LEFT_LOWER_FRONT, CENTER_LOWER_FRONT, RIGHT_LOWER_FRONT,
    LEFT_MID_FRONT,   CENTER_MID_FRONT,   RIGHT_MID_FRONT,
    LEFT_UPPER_FRONT, CENTER_UPPER_FRONT, RIGHT_UPPER_FRONT,
    LEFT_LOWER_REAR,  CENTER_LOWER_REAR,  RIGHT_LOWER_REAR,
    LEFT_MID_REAR,    CENTER_MID_REAR,    RIGHT_MID_REAR,
    LEFT_UPPER_REAR,  CENTER_UPPER_REAR,  RIGHT_UPPER_REAR
} LocationType;

static constexpr int32_t DiagFunctionStrLen = 12;

typedef struct fanTypeDescStruct {
    uint8_t type        {FAN_RPM};
    uint8_t ui8Zone     {1};
    uint8_t location    {LEFT_MID_REAR};
    uint8_t rsvd        {0}; // padding to get us to 16 bytes
    char    strFunction[DiagFunctionStrLen];
} FanTypeDescStruct;

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

class F0Ac : public VirtualSMCValue {
    _Atomic(uint16_t) *currentSpeed;

protected:
    SMC_RESULT readAccess() override;

public:
    F0Ac(_Atomic(uint16_t) *currentSpeed) : currentSpeed(currentSpeed) {}
};

class BDVT : public VirtualSMCValue {
    IOService *dst {nullptr};

protected:
    SMC_RESULT update(const SMC_DATA *src) override;

public:
    BDVT(IOService *dst) : dst(dst) {}
};

#endif /* KeyImplementations_hpp */
