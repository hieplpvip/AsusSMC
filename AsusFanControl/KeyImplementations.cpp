//
//  KeyImplementations.cpp
//  AsusFanControl
//
//  Fan SMC keys. Moved here from AsusSMC: a key can only be registered once, so
//  the keys travel with the code that provides them.
//

#include "KeyImplementations.hpp"
#include "AsusFanControl.hpp"

SMC_RESULT F0Ac::readAccess() {
    uint16_t speed = atomic_load_explicit(currentSpeed, memory_order_acquire);
    *reinterpret_cast<uint16_t *>(data) = VirtualSMCAPI::encodeIntFp(SmcKeyTypeFpe2, speed);
    return SmcSuccess;
}

SMC_RESULT F1Ac::readAccess() {
    uint16_t speed = atomic_load_explicit(currentSpeed, memory_order_acquire);
    *reinterpret_cast<uint16_t *>(data) = VirtualSMCAPI::encodeIntFp(SmcKeyTypeFpe2, speed);
    return SmcSuccess;
}

SMC_RESULT FConstAc::readAccess() {
    *reinterpret_cast<uint16_t *>(data) = VirtualSMCAPI::encodeIntFp(SmcKeyTypeFpe2, speed);
    return SmcSuccess;
}

SMC_RESULT FMode::readAccess() {
    uint32_t mode = atomic_load_explicit(currentMode, memory_order_acquire);
    reinterpret_cast<uint8_t *>(data)[0] = (uint8_t)(mode & 0xff);
    return SmcSuccess;
}

SMC_RESULT FMode::update(const SMC_DATA *src) {
    uint8_t mode = reinterpret_cast<const uint8_t *>(src)[0];

    AsusFanControl *drv = OSDynamicCast(AsusFanControl, fanService);
    if (drv == nullptr) {
        return SmcError;
    }

    // Non-zero means the mode cannot be honoured (manual PWM is not exposed by
    // boards without the AGFN 0x00110012 device). The reported mode is left
    // untouched so the caller can see that the write did not take.
    if (drv->setFanMode(index, mode) == 0) {
        reinterpret_cast<uint8_t *>(data)[0] = mode;
        return SmcSuccess;
    }
    return SmcError;
}

SMC_RESULT FThermalProfile::readAccess() {
    AsusFanControl *drv = OSDynamicCast(AsusFanControl, fanService);
    if (drv == nullptr) {
        return SmcError;
    }

    reinterpret_cast<uint8_t *>(data)[0] = (uint8_t)(drv->getThermalProfile() & 0xff);
    return SmcSuccess;
}

SMC_RESULT FThermalProfile::update(const SMC_DATA *src) {
    uint8_t profile = reinterpret_cast<const uint8_t *>(src)[0];

    AsusFanControl *drv = OSDynamicCast(AsusFanControl, fanService);
    if (drv == nullptr) {
        return SmcError;
    }

    // setThermalProfile rejects anything outside 0..2 before it reaches ACPI.
    // Returning SmcError makes the failed write visible to the caller rather
    // than silently pretending it worked.
    if (drv->setThermalProfile(profile) != 0) {
        return SmcError;
    }

    reinterpret_cast<uint8_t *>(data)[0] = profile;
    return SmcSuccess;
}
