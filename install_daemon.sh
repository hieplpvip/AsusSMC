#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

# remove AsusFnKeysDaemon
launchctl unload /Library/LaunchAgents/com.hieplpvip.AsusFnKeysDaemon.plist 2>/dev/null
pkill -f AsusFnKeysDaemon
sudo rm /usr/bin/AsusFnKeysDaemon 2>/dev/null
sudo rm /Library/LaunchAgents/com.hieplpvip.AsusFnKeysDaemon.plist 2>/dev/null

# unload old AsusSMCDaemon
launchctl unload /Library/LaunchAgents/com.hieplpvip.AsusSMCDaemon.plist 2>/dev/null

sudo cp $DIR/AsusSMCDaemon /usr/bin
sudo chmod 755 /usr/bin/AsusSMCDaemon
sudo chown root:wheel /usr/bin/AsusSMCDaemon

sudo cp $DIR/com.hieplpvip.AsusSMCDaemon.plist /Library/LaunchAgents
sudo chmod 644 /Library/LaunchAgents/com.hieplpvip.AsusSMCDaemon.plist
sudo chown root:wheel /Library/LaunchAgents/com.hieplpvip.AsusSMCDaemon.plist

launchctl load /Library/LaunchAgents/com.hieplpvip.AsusSMCDaemon.plist
