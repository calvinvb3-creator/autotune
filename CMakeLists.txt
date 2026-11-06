cmake_minimum_required(VERSION 3.22)
file(READ "${CMAKE_CURRENT_LIST_DIR}/version.json" pw_version_json)
string(JSON PW_VERSION GET "${pw_version_json}" version)
project(PitchWorkbench VERSION ${PW_VERSION} LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(PW_BUILD_PLUGIN "Build Audio Unit and VST3" ON)
option(PW_USE_INSTALLED_RUBBERBAND "Use a supplied system Rubber Band library" OFF)

if(PW_USE_INSTALLED_RUBBERBAND)
    find_library(PW_RUBBERBAND_LIBRARY NAMES rubberband REQUIRED)
    add_library(pw_rubberband UNKNOWN IMPORTED)
    set_target_properties(pw_rubberband PROPERTIES IMPORTED_LOCATION "${PW_RUBBERBAND_LIBRARY}")
else()
    include(FetchContent)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/rubberband/single/RubberBandSingle.cpp")
        set(FETCHCONTENT_SOURCE_DIR_RUBBERBAND "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/rubberband")
    endif()
    FetchContent_Declare(rubberband
        GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
        GIT_TAG v4.0.0 GIT_SHALLOW TRUE
        SOURCE_SUBDIR do-not-add-vendor-build)
    # Rubber Band uses Meson. Its documented single compilation unit lets us
    # statically link without a separate build system or Homebrew dylibs.
    FetchContent_MakeAvailable(rubberband)
    set(rb_single "${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp")
    if(NOT EXISTS "${rb_single}")
        message(FATAL_ERROR "Rubber Band single-file source is missing")
    endif()
    add_library(pw_rubberband STATIC "${rb_single}")
    # Releases may include the C wrapper in the single unit. Only add it when
    # it is not included there; prevent duplicate or missing API symbols.
    file(READ "${rb_single}" rb_single_text)
    if(NOT rb_single_text MATCHES "#[ \t]*include[^\n]*rubberband-c[.]cpp")
        target_sources(pw_rubberband PRIVATE "${rubberband_SOURCE_DIR}/src/rubberband-c.cpp")
    endif()
    target_include_directories(pw_rubberband PUBLIC "${rubberband_SOURCE_DIR}")
    target_compile_definitions(pw_rubberband INTERFACE PW_VENDOR_HEADER=1)
    find_package(Threads REQUIRED)
    target_link_libraries(pw_rubberband PUBLIC Threads::Threads)
endif()

enable_testing()
add_executable(pw_core_tests tests/core_tests.cpp)
target_include_directories(pw_core_tests PRIVATE Source)
target_link_libraries(pw_core_tests PRIVATE pw_rubberband)
add_test(NAME pitch_engine COMMAND pw_core_tests "${CMAKE_CURRENT_BINARY_DIR}")

if(PW_BUILD_PLUGIN)
    if(NOT APPLE)
        message(FATAL_ERROR "The plug-in target currently requires macOS. Use -DPW_BUILD_PLUGIN=OFF for engine tests.")
    endif()
    include(FetchContent)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JUCE/CMakeLists.txt")
        set(FETCHCONTENT_SOURCE_DIR_JUCE "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/JUCE")
    endif()
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.6 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
    juce_add_plugin(PitchWorkbench
        COMPANY_NAME "Independent Audio Development"
        BUNDLE_ID "local.pitchworkbench.effect"
        PLUGIN_MANUFACTURER_CODE Clvn
        PLUGIN_CODE PtWb
        FORMATS AU VST3
        PRODUCT_NAME "Pitch Workbench"
        IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx PitchShift)
    target_sources(PitchWorkbench PRIVATE Source/Processor.cpp Source/Editor.cpp)
    target_compile_definitions(PitchWorkbench PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    set_target_properties(PitchWorkbench PROPERTIES OSX_DEPLOYMENT_TARGET "12.0")
    target_link_libraries(PitchWorkbench PRIVATE pw_rubberband
        juce::juce_audio_utils juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags)
    # Also test the actual JUCE processor: state serialization, bus layouts,
    # host positions, capture, replay, variable block sizes, and bypass.
    add_executable(pw_host_tests tests/host_tests.cpp Source/Processor.cpp Source/Editor.cpp)
    target_include_directories(pw_host_tests PRIVATE Source)
    target_compile_definitions(pw_host_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(pw_host_tests PRIVATE pw_rubberband juce::juce_audio_utils juce::juce_gui_extra)
    add_test(NAME processor_host COMMAND pw_host_tests)
    juce_add_console_app(pw_format_tests PRODUCT_NAME "Pitch Workbench format checks")
    target_sources(pw_format_tests PRIVATE tests/format_tests.cpp)
    target_include_directories(pw_format_tests PRIVATE Source)
    target_compile_definitions(pw_format_tests PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(pw_format_tests PRIVATE juce::juce_audio_utils juce::juce_gui_extra)
    add_dependencies(pw_format_tests PitchWorkbench_VST3)
    add_test(NAME vst3_binary COMMAND pw_format_tests
        "${CMAKE_BINARY_DIR}/PitchWorkbench_artefacts/$<CONFIG>/VST3/Pitch Workbench.vst3")
endif()
