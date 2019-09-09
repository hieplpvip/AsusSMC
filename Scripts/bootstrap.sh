#!/bin/bash

#
#  bootstrap.sh
#
#  Copyright © 2019 hieplpvip. All rights reserved.
#
#  This script is supposed to quickly bootstrap Lilu/VirtualSMC SDK for plugin building.
#
#  Latest version available at:
#  https://raw.githubusercontent.com/hieplpvip/AsusSMC/master/Scripts/bootstrap.sh
#
#  Example usage:
#  src=$(/usr/bin/curl -Lfs https://raw.githubusercontent.com/hieplpvip/AsusSMC/master/Scripts/bootstrap.sh) && eval "$src" || exit 1
#

SDK_PATH="Lilu.kext"

PROJECT_PATH="$(pwd)"
if [ $? -ne 0 ] || [ ! -d "${PROJECT_PATH}" ]; then
  echo "ERROR: Failed to determine working directory!"
  exit 1
fi

# Avoid conflicts with PATH overrides.
CURL="/usr/bin/curl"
GIT="/usr/bin/git"
GREP="/usr/bin/grep"
MKDIR="/bin/mkdir"
MV="/bin/mv"
RM="/bin/rm"
SED="/usr/bin/sed"
UNAME="/usr/bin/uname"
UNZIP="/usr/bin/unzip"
UUIDGEN="/usr/bin/uuidgen"
XCODEBUILD="/usr/bin/xcodebuild"

TOOLS=(
  "${CURL}"
  "${GIT}"
  "${GREP}"
  "${MKDIR}"
  "${MV}"
  "${RM}"
  "${SED}"
  "${UNAME}"
  "${UNZIP}"
  "${UUIDGEN}"
  "${XCODEBUILD}"
)

for tool in "${TOOLS[@]}"; do
  if [ ! -x "${tool}" ]; then
    echo "ERROR: Missing ${tool}!"
    exit 1
  fi
done

# Prepare temporary directory to avoid conflicts with other scripts.
# Sets TMP_PATH.
prepare_environment() {
  local ret=0

  local sys=$("${UNAME}") || ret=$?
  if [ $ret -ne 0 ] || [ "$sys" != "Darwin" ]; then
    echo "ERROR: This script is only meant to be used on Darwin systems!"
    return 1
  fi

  local uuid=$("${UUIDGEN}") || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to generate temporary UUID with code ${ret}!"
    return 1
  fi

  TMP_PATH="/tmp/lilutmp.${uuid}"
  if [ -e "${TMP_PATH}" ]; then
    echo "ERROR: Found existing temporary directory ${TMP_PATH}, aborting!"
    return 1
  fi

  "${MKDIR}" "${TMP_PATH}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to create temporary directory ${TMP_PATH} with code ${ret}!"
    return 1
  fi

  cd "${TMP_PATH}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to cd to temporary directory ${TMP_PATH} with code ${ret}!"
    "${RM}" -rf "${TMP_PATH}"
    return 1
  fi

  return 0
}

# Install prebuilt Lilu SDK for release distribution.
install_lilu_sdk() {
  local ret=0

  echo "Installing prebuilt Lilu SDK..."

  echo "-> Obtaining release manifest..."

  echo "-> Cloning the latest version from master..."

  # This is a really ugly hack due to GitHub API rate limits.
  local url="https://github.com/acidanthera/Lilu"
  "${GIT}" clone "${url}" -b "master" "lilu" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to clone repository with code ${ret}!"
    return 1
  fi

  echo "-> Obtaining the latest tag..."

  cd "lilu" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to cd to temporary directory lilu with code ${ret}!"
    return 1
  fi

  local vers=$("${GIT}" describe --abbrev=0 --tags) || ret=$?
  if [ "$vers" = "" ]; then
    echo "ERROR: Failed to determine the latest release tag!"
    return 1
  fi

  echo "-> Discovered the latest tag ${vers}."

  cd .. || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to cd back from temporary directory with code ${ret}!"
    return 1
  fi

  "${RM}" -rf lilu || ret=$?
  if [ $ret -ne 0 ] || [ -d lilu ]; then
    echo "ERROR: Failed to remove temporary directory lilu with code ${ret}!"
    return 1
  fi

  local file="Lilu-${vers}-DEBUG.zip"

  echo "-> Downloading prebuilt debug version ${file}..."

  local url="https://github.com/acidanthera/Lilu/releases/download/${vers}/${file}"
  "${CURL}" -LfsO "${url}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to download ${file} with code ${ret}!"
    return 1
  fi

  echo "-> Extracting SDK from prebuilt debug version..."

  if [ ! -f "${file}" ]; then
    echo "ERROR: Failed to download ${file} to a non-existent location!"
    return 1
  fi

  "${MKDIR}" "lilu" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to create temporary directory at ${TMP_PATH} with code ${ret}!"
    return 1
  fi

  cd "lilu" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to cd to temporary directory lilu with code ${ret}!"
    return 1
  fi

  "${UNZIP}" -q ../"${file}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to unzip ${file} with code ${ret}!"
    return 1
  fi

  echo "-> Installing SDK from the prebuilt debug version..."

  "${RM}" -rf "${PROJECT_PATH}/Lilu.kext"
  "${MV}" "Lilu.kext" "${PROJECT_PATH}/Lilu.kext" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to install SDK with code ${ret}!"
    return 1
  fi

  echo "Installed prebuilt Lilu SDK ${vers}!"

  return 0
}

# Install prebuilt VirtualSMC SDK for release distribution.
install_vsmc_sdk() {
  local ret=0

  echo "Installing prebuilt VirtualSMC SDK..."

  echo "-> Obtaining release manifest..."

  echo "-> Cloning the latest version from master..."

  # This is a really ugly hack due to GitHub API rate limits.
  local url="https://github.com/acidanthera/VirtualSMC"
  "${GIT}" clone "${url}" -b "master" "vsmc" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to clone repository with code ${ret}!"
    return 1
  fi

  echo "-> Obtaining the latest tag..."

  cd "vsmc" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to cd to temporary directory vsmc with code ${ret}!"
    return 1
  fi

  local vers=$("${GIT}" describe --abbrev=0 --tags) || ret=$?
  if [ "$vers" = "" ]; then
    echo "ERROR: Failed to determine the latest release tag!"
    return 1
  fi

  echo "-> Discovered the latest tag ${vers}."

  cd .. || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to cd back from temporary directory with code ${ret}!"
    return 1
  fi

  "${RM}" -rf vsmc || ret=$?
  if [ $ret -ne 0 ] || [ -d vsmc ]; then
    echo "ERROR: Failed to remove temporary directory vsmc with code ${ret}!"
    return 1
  fi

  local file="VirtualSMC-${vers}-DEBUG.zip"

  echo "-> Downloading prebuilt debug version ${file}..."

  local url="https://github.com/acidanthera/VirtualSMC/releases/download/${vers}/${file}"
  "${CURL}" -LfsO "${url}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to download ${file} with code ${ret}!"
    return 1
  fi

  echo "-> Extracting SDK from prebuilt debug version..."

  if [ ! -f "${file}" ]; then
    echo "ERROR: Failed to download ${file} to a non-existent location!"
    return 1
  fi

  "${MKDIR}" "vsmc" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to create temporary directory at ${TMP_PATH} with code ${ret}!"
    return 1
  fi

  cd "vsmc" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to cd to temporary directory vsmc with code ${ret}!"
    return 1
  fi

  "${UNZIP}" -q ../"${file}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to unzip ${file} with code ${ret}!"
    return 1
  fi

  echo "-> Installing SDK from the prebuilt debug version..."

  "${RM}" -rf "${PROJECT_PATH}/VirtualSMC.kext"
  "${MV}" "Kexts/VirtualSMC.kext" "${PROJECT_PATH}/VirtualSMC.kext" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to install SDK with code ${ret}!"
    return 1
  fi

  echo "Installed prebuilt VirtualSMC SDK ${vers}!"

  return 0
}

prepare_environment || exit 1

ret=0

install_lilu_sdk || ret=$?

echo "------------------------------"

install_vsmc_sdk || ret=$?

cd "${PROJECT_PATH}" || ret=$?

"${RM}" -rf "${TMP_PATH}"

if [ $ret -ne 0 ]; then
  echo "ERROR: Failed to bootstrap SDK with code ${ret}!"
  exit 1
fi
