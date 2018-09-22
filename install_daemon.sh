#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

launchctl unload /Library/LaunchAgents/com.hieplpvip.AsusSMCDaemon.plist 2>/dev/null

sudo cp $DIR/AsusSMCDaemon /usr/bin
sudo chmod 755 /usr/bin/AsusSMCDaemon
sudo chown root:wheel /usr/bin/AsusSMCDaemon

sudo cp $DIR/com.hieplpvip.AsusSMCDaemon.plist /Library/LaunchAgents
sudo chmod 644 /Library/LaunchAgents/com.hieplpvip.AsusSMCDaemon.plist
sudo chown root:wheel /Library/LaunchAgents/com.hieplpvip.AsusSMCDaemon.plist

launchctl load /Library/LaunchAgents/com.hieplpvip.AsusSMCDaemon.plist
