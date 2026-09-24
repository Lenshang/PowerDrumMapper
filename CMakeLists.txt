cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(PowerDrumMapper
    VERSION 0.1.0
    DESCRIPTION "A CLAP MIDI note mapper for drum sound sources"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Dependencies: JUCE + clap-juce-extensions (which bundles the CLAP SDK)
# -----------------------------------------------------------------------------
add_subdirectory(libs/JUCE)
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

# -----------------------------------------------------------------------------
# The plugin target. It is a pure MIDI effect (no audio): it consumes MIDI
# note events and re-emits them with remapped note numbers / channels.
#
# The build target / product name is PowerDrumMapper; internal stable IDs
# (PLUGIN_MANUFACTURER_CODE, PLUGIN_CODE, CLAP_ID) are deliberately kept from
# the original BitwigDrumMapper release so existing host sessions resolve the
# same plugin identity.
#
# CLAP is added separately via clap_juce_extensions_plugin() below, which
# creates the `PowerDrumMapper_CLAP` target.
# -----------------------------------------------------------------------------
juce_add_plugin(PowerDrumMapper
    COMPANY_NAME            "PowerDrumMapper"
    PLUGIN_MANUFACTURER_CODE "BtDm"
    PLUGIN_CODE             "DrMp"
    FORMATS                 VST3 Standalone
    PRODUCT_NAME            "PowerDrumMapper"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       TRUE
    IS_MIDI_EFFECT          TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

# CLAP format target -> `PowerDrumMapper_CLAP`
clap_juce_extensions_plugin(
    TARGET       PowerDrumMapper
    CLAP_ID      "com.bitwigdrummapper.drummapper"
    CLAP_FEATURES note-effect utility)

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------
target_sources(PowerDrumMapper PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/NoteMapping.cpp
    Source/NoteNameUtils.cpp
    Source/MappingTableComponent.cpp)

# -----------------------------------------------------------------------------
# JUCE configuration: trim unused subsystems for faster / leaner builds
# -----------------------------------------------------------------------------
target_compile_definitions(PowerDrumMapper PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(PowerDrumMapper
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        clap_juce_extensions
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------------
# Headless test runner. Compiles the same sources as the plugin and exercises
# the remap logic, note-name queries, state persistence and CSV I/O without a
# host or audio device.
# -----------------------------------------------------------------------------
juce_add_console_app(PowerDrumMapperTests PRODUCT_NAME "PowerDrumMapperTests")

target_sources(PowerDrumMapperTests PRIVATE
    Tests/ProcessorTests.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/NoteMapping.cpp
    Source/NoteNameUtils.cpp
    Source/MappingTableComponent.cpp)

target_compile_definitions(PowerDrumMapperTests PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    # Plugin targets get these from juce_add_plugin; the test runner needs the
    # handful our sources actually reference spelled out manually.
    JucePlugin_Name="PowerDrumMapper"
    JucePlugin_Manufacturer="PowerDrumMapper"
    JucePlugin_ManufacturerCode=BtDm
    JucePlugin_PluginCode=DrMp)

target_link_libraries(PowerDrumMapperTests
    PRIVATE
        juce::juce_audio_utils
        clap_juce_extensions
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME PowerDrumMapperTests
         COMMAND PowerDrumMapperTests)
