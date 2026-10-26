cmake_minimum_required(VERSION 3.22)

# Must be set before project(); otherwise the app only runs on the build machine's macOS and CPU
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")

project(OSCFadeBridge VERSION 1.3.3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.7
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(OSCFadeBridge
    COMPANY_NAME     "Yaseen Clarke"
    PRODUCT_NAME     "OSC Fade Bridge"
    BUNDLE_ID        "com.yaseenclarke.oscfadebridge"
    ICON_BIG         "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png"
    # Without this macOS can silently block OSC to desks on the LAN (EHOSTUNREACH)
    PLIST_TO_MERGE   "<plist><dict><key>LSMinimumSystemVersion</key><string>12.0</string><key>NSLocalNetworkUsageDescription</key><string>OSC Fade Bridge sends OSC to mixing desks and other devices on your local network.</string></dict></plist>"
)

target_sources(OSCFadeBridge PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/BridgeEngine.cpp
    Source/MappingInspectorComponent.cpp
    Source/GlobalSettingsComponent.cpp
)

target_include_directories(OSCFadeBridge PRIVATE Source)

target_compile_definitions(OSCFadeBridge PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="OSC Fade Bridge"
    JUCE_APPLICATION_VERSION_STRING="1.3.3"
)

target_link_libraries(OSCFadeBridge
    PRIVATE
        juce::juce_gui_basics
        juce::juce_data_structures
        juce::juce_osc
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
