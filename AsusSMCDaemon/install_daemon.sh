#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

# remove AsusFnKeysDaemon
sudo launchctl unload /Library/LaunchAgents/com.hieplpvip.AsusFnKeysDaemon.plist 2>/dev/null
sudo pkill -f AsusFnKeysDaemon
sudo rm /usr/bin/AsusFnKeysDaemon 2>/dev/null
sudo rm /Library/LaunchAgents/com.hieplpvip.AsusFnKeysDaemon.plist 2>/dev/null

# remove old AsusSMCDaemon
sudo launchctl unload /Library/LaunchAgents/com.hieplpvip.AsusSMCDaemon.plist 2>/dev/null
sudo rm /usr/bin/AsusSMCDaemon 2>/dev/null

sudo mkdir -p /usr/local/bin/
sudo chmod -R 755 /usr/local/bin/
sudo cp $DIR/AsusSMCDaemon /usr/local/bin/
sudo chmod 755 /usr/local/bin/AsusSMCDaemon
sudo chown root:wheel /usr/local/bin/AsusSMCDaemon

sudo cp $DIR/com.hieplpvip.AsusSMCDaemon.plist /Library/LaunchAgents
sudo chmod 644 /Library/LaunchAgents/com.hieplpvip.AsusSMCDaemon.plist
sudo chown root:wheel /Library/LaunchAgents/com.hieplpvip.AsusSMCDaemon.plist

sudo launchctl load /Library/LaunchAgents/com.hieplpvip.AsusSMCDaemon.plist
