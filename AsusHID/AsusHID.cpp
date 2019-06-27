//
//  AsusHID.cpp
//  AsusSMC
//
//  Copyright © 2019 Le Bao Hiep
//

#include "AsusHID.hpp"

#define super IOService
OSDefineMetaClassAndStructors(AsusHID, IOService);

bool AsusHID::start(IOService *provider) {
    DBGLOG("hid", "start is called");

    if (!super::start(provider)) {
        SYSLOG("hid", "Error loading HID driver");
        return false;
    }

    this->attach(provider);

    auto dict = propertyMatching(OSSymbol::withCString("AsusHID Supported"), kOSBooleanTrue);
    auto asusSMC = IOService::waitForMatchingService(dict, 5000000000); // wait for 5 secs
    dict->release();

    if (asusSMC) {
        asusSMC->message(kAddAsusHID, this);
        DBGLOG("hid", "Connected with AsusSMC");
    }

    setProperty("Copyright", "Copyright © 2019 hieplpvip");
    setProperty(kIOHIDTransportKey, provider->copyProperty(kIOHIDTransportKey));
    setProperty(kIOHIDManufacturerKey, provider->copyProperty(kIOHIDManufacturerKey));
    setProperty(kIOHIDProductKey, provider->copyProperty(kIOHIDProductKey));
    setProperty(kIOHIDLocationIDKey, provider->copyProperty(kIOHIDLocationIDKey));
    setProperty(kIOHIDVersionNumberKey, provider->copyProperty(kIOHIDVersionNumberKey));
    return true;
}

void AsusHID::stop(IOService *provider) {
    DBGLOG("hid", "stop is called");

    if (asusSMC) {
        asusSMC->message(kDelAsusHID, this);
        DBGLOG("hid", "Disconnected with AsusSMC");
    }
    OSSafeReleaseNULL(asusSMC);

    super::stop(provider);
}
