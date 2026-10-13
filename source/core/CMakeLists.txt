# studio_core: the engine, DSP, generators, project model and release tools.
# It also carries the JUCE module code once, so the app, the CLI and the tests
# link this library rather than compiling JUCE three times.

file(GLOB_RECURSE STUDIO_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_library(studio_core STATIC ${STUDIO_CORE_SOURCES})

target_include_directories(studio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_compile_definitions(studio_core
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=1
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_LAME_AUDIO_FORMAT=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_PLUGINHOST_LV2=0
        JUCE_ASIO=0
        JUCE_JACK=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_UNIT_TESTS=1
        JUCE_SILENCE_XCODE_15_LINKER_WARNING=1)

target_link_libraries(studio_core
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        SQLite::SQLite3)

if(STUDIO_ENABLE_LTO)
    target_link_libraries(studio_core PUBLIC juce::juce_recommended_lto_flags)
endif()

# Re-export JUCE's include paths and module definitions so that consumers
# can include <juce_*/juce_*.h> without compiling the modules again.
target_include_directories(studio_core INTERFACE
    $<TARGET_PROPERTY:studio_core,INCLUDE_DIRECTORIES>)
target_compile_definitions(studio_core INTERFACE
    $<TARGET_PROPERTY:studio_core,COMPILE_DEFINITIONS>)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(STUDIO_LINUX_DEPS REQUIRED alsa freetype2 fontconfig)
    target_link_libraries(studio_core PUBLIC ${STUDIO_LINUX_DEPS_LIBRARIES} pthread dl rt)

    # Pack downloads use libcurl on Linux; without it the app still installs packs from files.
    find_package(CURL QUIET)
    if(CURL_FOUND)
        target_compile_definitions(studio_core PUBLIC JUCE_USE_CURL=1 JUCE_LOAD_CURL_SYMBOLS_LAZILY=1)
    else()
        target_compile_definitions(studio_core PUBLIC JUCE_USE_CURL=0)
    endif()
endif()

if(MSVC)
    target_compile_options(studio_core PUBLIC /bigobj)
endif()
