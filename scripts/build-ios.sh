#!/usr/bin/env bash
# Builds the iOS app on a Mac and packs an unsigned .ipa (see docs/IOS.md).
#
#   ./scripts/build-ios.sh            # arm64 device build
#   ./scripts/build-ios.sh simulator  # arm64 simulator build (for the Simulator, no .ipa)
#
# Requires Xcode + command line tools, CMake 3.22+.
set -euo pipefail

cd "$(dirname "$0")/.."

MODE="${1:-device}"

if [ "$MODE" = "simulator" ]; then
    SYSROOT="iphonesimulator"
    TARGET_PATH="-iphonesimulator"
else
    SYSROOT="iphoneos"
    TARGET_PATH="-iphoneos"
fi

cmake -S . -B build-ios -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$SYSROOT" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DLYNX_LTO=OFF \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO \
    "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY="

cmake --build build-ios --config Release --target LynxIOS -- -sdk "$SYSROOT" -quiet

APP=$(find build-ios -type d -name "LYNX.app" -path "*${TARGET_PATH#-}*" | head -n 1)

if [ -z "$APP" ]; then
    APP=$(find build-ios -type d -name "LYNX.app" | head -n 1)
fi

if [ -z "$APP" ]; then
    echo "LYNX.app not found under build-ios" >&2
    exit 1
fi

echo "Built: $APP"

if [ "$MODE" = "simulator" ]; then
    echo "Open it with: xcrun simctl install booted \"$APP\""
    exit 0
fi

rm -rf Payload LYNX-unsigned.ipa
mkdir -p Payload
cp -R "$APP" Payload/
zip -qry LYNX-unsigned.ipa Payload
echo "Wrote $(pwd)/LYNX-unsigned.ipa"
