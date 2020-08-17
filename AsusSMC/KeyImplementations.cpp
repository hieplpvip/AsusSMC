//
//  KeyImplementations.cpp
//  AsusSMC
//
//  Copyright © 2018-2020 Le Bao Hiep. All rights reserved.
//

#include "KeyImplementations.hpp"

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
    uint16_t tval = (value->val1 << 4) | (value->val2 >> 4);
    DBGLOG("kbrdblight", "LKSB update %d", tval);
    tval /= 16;

    if (atkDevice) {
        // Call ACPI method to adjust keyboard backlight
        OSNumber *arg = OSNumber::withNumber(tval, sizeof(tval) * 8);
        atkDevice->evaluateObject("SKBV", NULL, (OSObject**)&arg, 1);
        arg->release();
    }

    OSCollectionIterator *i = OSCollectionIterator::withCollection(_hidDrivers);
    if (i != NULL) {
        while (AsusHIDDriver *hid = OSDynamicCast(AsusHIDDriver, i->getNextObject()))
            hid->setKeyboardBacklight(tval);
        i->release();
    }

    delete value;

    // Write value to SMC
    lilu_os_memcpy(data, src, size);
    return SmcSuccess;
}
