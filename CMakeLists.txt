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
# WebView2 SDK pre-location (Windows, for the embedded WebView UI).
#
# JUCE's FindWebView2 script discovers the Microsoft.Web.WebView2 NuGet
# package by globbing the local NuGet cache with a *case-sensitive* pattern.
# PackageManagement may cache the folder lowercase (e.g. "microsoft.web.webview2"),
# in which case the glob silently matches nothing and the build fails with a
# missing WebView2.h. `if(EXISTS)` / `file(GLOB *)` here resolve paths
# case-insensitively, so we locate the SDK ourselves and seed JUCE's cache
# variable. Set -DJUCE_WEBVIEW2_PACKAGE_LOCATION=... to override.
# -----------------------------------------------------------------------------
if(WIN32 AND NOT JUCE_WEBVIEW2_PACKAGE_LOCATION AND NOT WebView2_root_dir)
    file(GLOB _pdm_nuget_entries "$ENV{USERPROFILE}/AppData/Local/PackageManagement/NuGet/Packages/*")

    foreach(_entry IN LISTS _pdm_nuget_entries)
        get_filename_component(_name "${_entry}" NAME)
        string(TOLOWER "${_name}" _name_lower)

        if(_name_lower MATCHES "^microsoft\\.web\\.webview2")
            file(GLOB _versions "${_entry}/*")
            list(SORT _versions)
            list(REVERSE _versions)

            foreach(_version_dir IN LISTS _versions)
                if(EXISTS "${_version_dir}/build/native/include/WebView2.h")
                    set(WebView2_root_dir "${_version_dir}" CACHE PATH
                        "Root of the Microsoft.Web.WebView2 SDK package" FORCE)
                    message(STATUS "WebView2 SDK found at ${_version_dir}")
                    break()
                endif()
            endforeach()
        endif()
    endforeach()

    unset(_pdm_nuget_entries)
    unset(_name)
    unset(_name_lower)
    unset(_versions)
    unset(_version_dir)
endif()

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
    COPY_PLUGIN_AFTER_BUILD TRUE
    # Static WebView2 loader for the embedded UI (Windows; ignored elsewhere).
    NEEDS_WEBVIEW2          TRUE)

# CLAP format target -> `PowerDrumMapper_CLAP`
clap_juce_extensions_plugin(
    TARGET       PowerDrumMapper
    CLAP_ID      "com.bitwigdrummapper.drummapper"
    CLAP_FEATURES note-effect utility)

# clap-juce-extensions honours COPY_PLUGIN_AFTER_BUILD for the CLAP only on
# macOS/Linux; mirror its Windows post-build install so Bitwig always sees the
# freshly built CLAP (needs an elevated shell to write into Common Files).
if(WIN32)
    add_custom_command(TARGET PowerDrumMapper_CLAP POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo
                "Installing $<TARGET_FILE_NAME:PowerDrumMapper_CLAP> to %COMMONPROGRAMFILES%\\CLAP"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{COMMONPROGRAMFILES}/CLAP"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:PowerDrumMapper_CLAP>"
                "$ENV{COMMONPROGRAMFILES}/CLAP/$<TARGET_FILE_NAME:PowerDrumMapper_CLAP>"
        VERBATIM)
endif()

# -----------------------------------------------------------------------------
# The embedded WebView UI (served to the WebBrowserComponent through its
# resource provider; nothing is read from disk at runtime).
# -----------------------------------------------------------------------------
juce_add_binary_data(PowerDrumMapperWebUI
    NAMESPACE   PowerDrumMapperWebUI
    HEADER_NAME PowerDrumMapperWebUI.h
    SOURCES     Source/WebUI/index.html
                Source/WebUI/style.css
                Source/WebUI/app.js)

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------
target_sources(PowerDrumMapper PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/WebViewBridge.cpp
    Source/NoteMapping.cpp
    Source/NoteNameUtils.cpp)

# -----------------------------------------------------------------------------
# JUCE configuration: trim unused subsystems for faster / leaner builds
# -----------------------------------------------------------------------------
target_compile_definitions(PowerDrumMapper PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

# The embedded UI requires the WebView2 backend (Chromium) on Windows; without
# this the WebBrowserComponent would fall back to the legacy IE engine, which
# cannot run the page. Set alongside NEEDS_WEBVIEW2 above.
if(WIN32)
    target_compile_definitions(PowerDrumMapper PUBLIC
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
endif()

target_link_libraries(PowerDrumMapper
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        PowerDrumMapperWebUI
        clap_juce_extensions
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------------
# Headless test runner. Compiles the same sources as the plugin (except the
# WebView editor, which is replaced by Tests/EditorStub.cpp) and exercises the
# remap logic, note-name queries, state persistence and CSV I/O without a host
# or audio device.
# -----------------------------------------------------------------------------
juce_add_console_app(PowerDrumMapperTests PRODUCT_NAME "PowerDrumMapperTests")

target_sources(PowerDrumMapperTests PRIVATE
    Tests/ProcessorTests.cpp
    Tests/EditorStub.cpp
    Source/PluginProcessor.cpp
    Source/NoteMapping.cpp
    Source/NoteNameUtils.cpp)

target_compile_definitions(PowerDrumMapperTests PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
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
