cmake_minimum_required(VERSION 3.22)
project(KloudVocalShift VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The DSP core has no JUCE dependency, so its tests and the offline profiler
# build and run on a machine with no audio framework at all -- including a CI
# container. Configure with -DKVS_DSP_ONLY=ON to skip the plugin target and
# everything JUCE brings with it.
option(KVS_DSP_ONLY "Build only the JUCE-free DSP tests and the profiler" OFF)

if(NOT KVS_DSP_ONLY)
    # Audio Units are macOS-only; everywhere else it is VST3 plus the standalone.
    set(KVS_FORMATS VST3 Standalone)

    if(APPLE)
        list(APPEND KVS_FORMATS AU)

        # Dev builds are native arm64. For a release universal binary, configure
        # with -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
        if(NOT CMAKE_OSX_ARCHITECTURES)
            set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
        endif()

        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()

    # Only development builds install into the user's plugin folders. A Release
    # build is for packaging and must not clobber the binary you are testing with.
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(KVS_INSTALL_AFTER_BUILD FALSE)
    else()
        set(KVS_INSTALL_AFTER_BUILD TRUE)
    endif()

    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    add_subdirectory(libs/JUCE)

    juce_add_plugin(KloudVocalShift
        COMPANY_NAME                "LT3 Audio"
        BUNDLE_ID                   "com.lt3audio.kloudvocalshift"
        PLUGIN_MANUFACTURER_CODE    LT3a
        PLUGIN_CODE                 Kvsh
        FORMATS                     ${KVS_FORMATS}
        PRODUCT_NAME                "KloudVocalShift"
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     ${KVS_INSTALL_AFTER_BUILD}
        VST3_CATEGORIES             "Fx" "Pitch Shift"
        AU_MAIN_TYPE                kAudioUnitType_Effect)

    target_sources(KloudVocalShift PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/params/ParameterLayout.cpp
        src/dsp/SpectralStage.cpp
    src/dsp/WarpChain.cpp
        src/dsp/DspCore.cpp
        src/gui/LookAndFeel.cpp
        src/gui/Controls.cpp
        src/gui/WarpReadout.cpp)

    target_include_directories(KloudVocalShift PRIVATE src)

    target_compile_definitions(KloudVocalShift PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0)

    target_link_libraries(KloudVocalShift
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

#==============================================================================
# Tests.
#==============================================================================
enable_testing()

set(KVS_DSP_SOURCES
    src/dsp/SpectralStage.cpp
    src/dsp/WarpChain.cpp
    src/dsp/DspCore.cpp)

# Plain C++, no framework. This is where the transparency claims live, and those
# are the ones that would break every saved project if they stopped holding, so
# they have to stay runnable everywhere, cheaply, on every push.
add_executable(KloudVocalShiftDspTests tests/DspTests.cpp ${KVS_DSP_SOURCES})
target_include_directories(KloudVocalShiftDspTests PRIVATE src tests)
add_test(NAME dsp COMMAND KloudVocalShiftDspTests)

add_executable(measure tools/measure/main.cpp ${KVS_DSP_SOURCES})
target_include_directories(measure PRIVATE src tests)

if(NOT KVS_DSP_ONLY)
    foreach(target KloudVocalShiftDspTests measure)
        target_link_libraries(${target} PRIVATE
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    endforeach()

    # A plain executable rather than juce_add_console_app: the latter forces
    # JUCE_STANDALONE_APPLICATION=1, which collides with the definitions the
    # plugin target exports.
    add_executable(KloudVocalShiftParamTests tests/ParameterTests.cpp)
    target_include_directories(KloudVocalShiftParamTests PRIVATE src)
    target_link_libraries(KloudVocalShiftParamTests PRIVATE
        KloudVocalShift
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_events
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    add_test(NAME parameters COMMAND KloudVocalShiftParamTests)
endif()
