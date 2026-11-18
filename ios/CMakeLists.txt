# iOS app: LYNX SHARE's JUCE-free core (lynx_core) plus a touch-first front end.
#
# Everything here is driven by .github/workflows/ios.yml, which builds an unsigned .ipa on a macOS runner:
#   cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
#   cmake --build build-ios --config Release --target LynxIOS
#
# The desktop target (VST3/CLAP/AU hosting, ASIO/ALSA, multi-window workspace) is not built for iOS; see docs/IOS.md.

set(LYNX_IOS_SOURCES
    ${PROJECT_SOURCE_DIR}/src/ios/IosMain.cpp
    ${PROJECT_SOURCE_DIR}/src/ios/IosAudioEngine.cpp
    ${PROJECT_SOURCE_DIR}/src/ios/IosRootComponent.cpp
    # the native .lynx project format (JUCE JSON) and its model mapping
    ${PROJECT_SOURCE_DIR}/src/engine/ProjectFile.cpp)

juce_add_gui_app(LynxIOS
    VERSION "${PROJECT_VERSION}"
    PRODUCT_NAME "LYNX"
    BUNDLE_ID "com.ka1-s.lynx.ios"
    COMPANY_NAME "Ka1-S"
    COMPANY_COPYRIGHT "Copyright (C) 2026 Ka1-S. Licensed under the GNU AGPL-3.0."
    DESCRIPTION "LYNX SHARE digital audio workstation"
    ICON_BIG "${PROJECT_SOURCE_DIR}/resources/macos/lynx-icon-1024.png"
    ICON_SMALL "${PROJECT_SOURCE_DIR}/resources/macos/lynx-icon-256.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "LYNX SHARE records audio from the microphone when you arm a track and press Record."
    DOCUMENT_EXTENSIONS lynx flp als flm
    # the app's Documents folder is the place projects are put into from the Files app (docs/IOS.md)
    FILE_SHARING_ENABLED TRUE
    STATUS_BAR_HIDDEN TRUE
    REQUIRES_FULL_SCREEN TRUE
    IPHONE_SCREEN_ORIENTATIONS
        UIInterfaceOrientationLandscapeLeft
        UIInterfaceOrientationLandscapeRight
    IPAD_SCREEN_ORIENTATIONS
        UIInterfaceOrientationLandscapeLeft
        UIInterfaceOrientationLandscapeRight
        UIInterfaceOrientationPortrait
    PLIST_TO_MERGE
        "<plist><dict>
            <key>UIRequiredDeviceCapabilities</key><array><string>arm64</string></array>
            <key>LSSupportsOpeningDocumentsInPlace</key><true/>
            <key>UIApplicationSupportsIndirectInputEvents</key><true/>
            <key>ITSAppUsesNonExemptEncryption</key><false/>
        </dict></plist>")

target_sources(LynxIOS PRIVATE ${LYNX_IOS_SOURCES})

# tools/ holds WavReader.h, the sample decoder the audio engine uses (the same one renderwav uses)
target_include_directories(LynxIOS PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/tools)

target_compile_definitions(LynxIOS PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=0
    JUCE_PLUGINHOST_AU=0
    JUCE_USE_MP3AUDIOFORMAT=0
    # the same definitions the desktop app gets (src/engine/ProjectFile.cpp writes the version into saved projects)
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LynxIOS,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LynxIOS,JUCE_VERSION>"
    LYNX_IOS=1)

target_link_libraries(LynxIOS
    PRIVATE
        lynx_core
        juce::juce_audio_devices
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags)

# juce_gui_basics' iOS code references UNUserNotificationCenter without declaring the framework
find_library(LYNX_IOS_USER_NOTIFICATIONS UserNotifications REQUIRED)
target_link_libraries(LynxIOS PRIVATE ${LYNX_IOS_USER_NOTIFICATIONS})

# iPhone and iPad, and no bitcode (deprecated since Xcode 14)
set_target_properties(LynxIOS PROPERTIES
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO")

