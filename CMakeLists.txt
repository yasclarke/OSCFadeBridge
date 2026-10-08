cmake_minimum_required(VERSION 3.22)
project(OSCFadeBridge VERSION 1.2.0)

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
    JUCE_APPLICATION_VERSION_STRING="1.2.0"
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
