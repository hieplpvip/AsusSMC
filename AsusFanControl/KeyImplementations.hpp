//
//  KeyImplementations.hpp
//  AsusFanControl
//
//  The SMC keys for fan telemetry and fan-mode control.
//
//  These used to live in AsusSMC's KeyImplementations. They are here because an
//  SMC key can only be registered once - VirtualSMC rejects a second
//  registration of the same key - so the keys had to move with the code that
//  provides them, not be shared between the two kexts.
//
//  Copyright © 2018-2020 Le Bao Hiep. All rights reserved.
//

#ifndef AsusFanControl_KeyImplementations_hpp
#define AsusFanControl_KeyImplementations_hpp

#include <IOKit/IOService.h>
#include <VirtualSMCSDK/kern_vsmcapi.hpp>

// --- SMC key identifiers ------------------------------------------------
static constexpr SMC_KEY KeyFNum = SMC_MAKE_IDENTIFIER('F','N','u','m');
static constexpr SMC_KEY KeyF0ID = SMC_MAKE_IDENTIFIER('F','0','I','D');
static constexpr SMC_KEY KeyF0Ac = SMC_MAKE_IDENTIFIER('F','0','A','c');
static constexpr SMC_KEY KeyF1ID = SMC_MAKE_IDENTIFIER('F','1','I','D');
static constexpr SMC_KEY KeyF1Ac = SMC_MAKE_IDENTIFIER('F','1','A','c');

// Macs Fan Control reads F0Mn / F0Mx to know the usable range. Both are fpe2,
// the same encoding as F0Ac.
static constexpr SMC_KEY KeyF0Mn = SMC_MAKE_IDENTIFIER('F','0','M','n');
static constexpr SMC_KEY KeyF0Mx = SMC_MAKE_IDENTIFIER('F','0','M','x');
static constexpr SMC_KEY KeyF1Mn = SMC_MAKE_IDENTIFIER('F','1','M','n');
static constexpr SMC_KEY KeyF1Mx = SMC_MAKE_IDENTIFIER('F','1','M','x');

// The pwm1_enable equivalent: per-fan full speed (0) or automatic (2).
static constexpr SMC_KEY KeyF0Md = SMC_MAKE_IDENTIFIER('F','0','M','d');
static constexpr SMC_KEY KeyF1Md = SMC_MAKE_IDENTIFIER('F','1','M','d');

// Thermal profile selector: 0 = balanced, 1 = performance, 2 = silent. It picks
// which firmware fan curve the EC runs and applies to both fans, so it is one
// key rather than one per fan. Named F0Tp to keep it clearly distinct from F0Md.
static constexpr SMC_KEY KeyF0Tp = SMC_MAKE_IDENTIFIER('F','0','T','p');

// --- diagnostic descriptor ----------------------------------------------
// Matches the layout Apple uses for fan keys: type, zone, location, padding to
// 16 bytes, then a short function name for tools that display it.
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
    uint8_t rsvd        {0}; // padding to reach 16 bytes
    char    strFunction[DiagFunctionStrLen];
} FanTypeDescStruct;

// --- value implementations ----------------------------------------------

// FxAc: current fan speed, fpe2 encoded (RPM * 4).
class F0Ac : public VirtualSMCValue {
    _Atomic(uint16_t) *currentSpeed;

protected:
    SMC_RESULT readAccess() override;

public:
    F0Ac(_Atomic(uint16_t) *currentSpeed) : currentSpeed(currentSpeed) {}
};

// The same for the second fan.
class F1Ac : public VirtualSMCValue {
    _Atomic(uint16_t) *currentSpeed;

protected:
    SMC_RESULT readAccess() override;

public:
    F1Ac(_Atomic(uint16_t) *currentSpeed) : currentSpeed(currentSpeed) {}
};

// A constant fpe2 value, used for the FxMn / FxMx range keys.
class FConstAc : public VirtualSMCValue {
    uint16_t speed;

protected:
    SMC_RESULT readAccess() override;

public:
    FConstAc(uint16_t speed) : speed(speed) {}
};

// FxMd: 0 = firmware full speed, 2 = automatic. Manual PWM is not offered:
// boards without the 0x00110012 device have no per-fan PWM.
class FMode : public VirtualSMCValue {
    _Atomic(uint32_t) *currentMode;
    IOService *fanService {nullptr};
    uint8_t index {0};

protected:
    SMC_RESULT readAccess() override;
    SMC_RESULT update(const SMC_DATA *src) override;

public:
    FMode(IOService *fanService, uint8_t index, _Atomic(uint32_t) *currentMode) :
        currentMode(currentMode), fanService(fanService), index(index) {}
};

// F0Tp: thermal profile. Writes are validated inside the driver, so an
// out-of-range value can never reach the DSDT.
class FThermalProfile : public VirtualSMCValue {
    IOService *fanService {nullptr};

protected:
    SMC_RESULT readAccess() override;
    SMC_RESULT update(const SMC_DATA *src) override;

public:
    FThermalProfile(IOService *fanService) : fanService(fanService) {}
};

// Registered by AsusFanControl::startVirtualSMC().
void registerFanKeys();

#endif // AsusFanControl_KeyImplementations_hpp
