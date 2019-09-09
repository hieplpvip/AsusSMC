#!/bin/bash

if [ "${TARGET_BUILD_DIR}" = "" ]; then
  echo "This must not be run outside of Xcode"
  exit 1
fi

cd "${TARGET_BUILD_DIR}"

# clean / build
if [ "$1" != "analyze" ]; then
  rm -rf *.zip || exit 1
fi

cp "${SRCROOT}/AsusSMCDaemon/com.hieplpvip.AsusSMCDaemon.plist" ./ || exit 1
cp "${SRCROOT}/AsusSMCDaemon/install_daemon.sh" ./ || exit 1

archive="AsusSMC-${MODULE_VERSION}-$(echo $CONFIGURATION | tr /a-z/ /A-Z/).zip"
zip -qry -FS ../"${archive}" * || exit 1
