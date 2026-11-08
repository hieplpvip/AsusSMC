//
//  KeyImplementations.cpp
//  AsusSMC
//
//  Copyright 漏 2018-2020 Le Bao Hiep. All rights reserved.
//

#include "KeyImplementations.hpp"
#include "AsusSMC.hpp"

SMC_RESULT SMCALSValue::readAccess() {
    auto value = reinterpret_cast<Value *>(data);
    uint32_t lux = atomic_load_explicit(currentLux, memory_order_acquire);
    uint8_t bits = forceBits->bits();

    if (lux == 0xFFFFFFFF) {
        value->valid = false;
    } else {
        value->valid = true;
        if (!(bits & ALSForceBits::kALSForceHighGain))
            value->highGain = true;
        if (!(bits & ALSForceBits::kALSForceChan))
            value->chan0 = OSSwapHostToBigInt16(lux);
        if (!(bits & ALSForceBits::kALSForceLux))
            value->roomLux = OSSwapHostToBigInt32(lux << 14);
    }

    return SmcSuccess;
}

SMC_RESULT SMCKBrdBLightValue::update(const SMC_DATA *src)  {
    lkb *value = new lkb;
    lilu_os_memcpy(value, src, size);

    // LKSB carries the level in val1, with val2 holding flags; the default
    // initialiser is {val1 = 0, val2 = 1}. Upstream recovered the level with
    // tval / 16, which is consistent with val1 being the 0..16 level.
    //
    // An earlier revision here scaled the whole packed value against 0xFFB
    // instead. That collapsed every slider position to level 0, which showed
    // up as "the brightness control does nothing" and as the backlight going
    // dark at boot - macOS writes the restored level, and the driver turned
    // it into 0. Reverted to the val1 interpretation.
    uint16_t packed = (value->val1 << 4) | (value->val2 >> 4);

    // Stay correct for both plausible encodings: a direct 0..16 step count,
    // or a 12-bit scale. Anything at or below 16 * 16 is the former.
    uint32_t steps;
    if (packed <= 16 * 16) {
        steps = (packed + 8) / 16;
    } else {
        steps = ((uint32_t)packed * 16 + 2047) / 4095;
    }
    if (steps > 16) {
        steps = 16;
    }

    uint16_t level = (uint16_t)((steps * ASUS_KBD_BACKLIGHT_MAX + 8) / 16);
    if (level > ASUS_KBD_BACKLIGHT_MAX) {
        level = ASUS_KBD_BACKLIGHT_MAX;
    }

    // Record the raw bytes. Without them there is no way to tell which
    // encoding macOS actually uses:
    //   ioreg -rn AsusSMC -l | grep AsusSMCLKSB
    AsusSMC *inst = OSDynamicCast(AsusSMC, asusSMCInstance);
    if (inst) {
        inst->recordKbdWrite(value->val1, value->val2, packed, level);
        // Also bump the LKSB write counter. Without this lksbW stayed at 0
        // even when writes were arriving, so the diagnostics were lying.
        inst->recordLKSAccess(0, true, packed);
    }

    DBGLOG("lksb", "LKSB update val1=%u val2=%u packed=%u -> level %u",
           value->val1, value->val2, packed, level);

    delete value;

    if (asusSMCInstance) {
        asusSMCInstance->message(kSetKeyboardBacklightMessage, nullptr, &level);
    }

    lilu_os_memcpy(data, src, size);
    return SmcSuccess;
}

SMC_RESULT SMCKBrdBLightValue::readAccess() {
    AsusSMC *inst = OSDynamicCast(AsusSMC, asusSMCInstance);
    if (inst) {
        inst->recordLKSAccess(0, false, 0);
    }
    return VirtualSMCValue::readAccess();
}

SMC_RESULT SMCKBrdBLightState::readAccess() {
    AsusSMC *inst = OSDynamicCast(AsusSMC, asusSMCInstance);
    if (inst) {
        inst->recordLKSAccess(1, false, 0);
    }
    return VirtualSMCValue::readAccess();
}

SMC_RESULT SMCKBrdBLightState::update(const SMC_DATA *src) {
    AsusSMC *inst = OSDynamicCast(AsusSMC, asusSMCInstance);
    if (inst && src && size >= 1) {
        inst->recordLKSAccess(1, true, (uint16_t)src[0]);
    }
    return VirtualSMCValue::update(src);
}

SMC_RESULT BDVT::update(const SMC_DATA *src)  {
    bool state = false;
    lilu_os_memcpy(&state, src, size);

    // BDVT is 00 when battery health is enabled and 01 when disabled
    state = !state;

    AsusSMC *drv = OSDynamicCast(AsusSMC, dst);
    drv->toggleBatteryConservativeMode(state);

    lilu_os_memcpy(data, src, size);
    return SmcSuccess;
}
