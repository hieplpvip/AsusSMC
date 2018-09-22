#!/bin/bash

sudo rm -rf build
sudo xcodebuild -alltargets -configuration Debug
sudo xcodebuild -alltargets -configuration Release

sudo cp ./AsusSMCDaemon/com.hieplpvip.AsusSMCDaemon.plist ./build/Debug
sudo cp ./AsusSMCDaemon/com.hieplpvip.AsusSMCDaemon.plist ./build/Release

sudo cp ./install_daemon.sh ./build/Debug
sudo cp ./install_daemon.sh ./build/Release

sudo chown -R root:wheel ./build/Debug/AsusSMC.kext
sudo chmod -R 755 ./build/Debug/AsusSMC.kext
sudo chown -R root:wheel ./build/Release/AsusSMC.kext
sudo chmod -R 755 ./build/Release/AsusSMC.kext
