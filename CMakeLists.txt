cmake_minimum_required(VERSION 3.22)

if(DEFINED ENV{VERSION} AND NOT "$ENV{VERSION}" STREQUAL "")
    set(ISFX_VERSION "$ENV{VERSION}")
else()
    set(ISFX_VERSION "1.0.0")
endif()
project(InSequenceFX VERSION ${ISFX_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
    add_compile_options(/bigobj)
endif()

include(FetchContent)
set(JUCE_VERSION 8.0.4 CACHE STRING "JUCE version to fetch")
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG ${JUCE_VERSION} GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# --- FFmpeg (libavformat/libavcodec/libavutil/libswscale/libswresample) -------
# Linux/macOS: pkg-config (macOS CI builds an LGPL FFmpeg into FFMPEG_DIR, see scripts/build_ffmpeg_macos.sh)
# Windows: FFMPEG_DIR points at an extracted BtbN *-lgpl-shared build (include/, lib/, bin/)
set(FFMPEG_DIR "" CACHE PATH "FFmpeg install prefix")
add_library(isfx_ffmpeg INTERFACE)
if(WIN32)
    if(NOT FFMPEG_DIR)
        message(FATAL_ERROR "Set -DFFMPEG_DIR to an FFmpeg shared build")
    endif()
    target_include_directories(isfx_ffmpeg INTERFACE "${FFMPEG_DIR}/include")
    foreach(l avformat avcodec avutil swscale swresample)
        target_link_libraries(isfx_ffmpeg INTERFACE "${FFMPEG_DIR}/lib/${l}.lib")
    endforeach()
else()
    if(FFMPEG_DIR)
        set(ENV{PKG_CONFIG_PATH} "${FFMPEG_DIR}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
    target_link_libraries(isfx_ffmpeg INTERFACE PkgConfig::FFMPEG)
endif()

set(ISFX_CORE_SOURCES
    Source/Model.cpp
    Source/Media.cpp
    Source/Analysis.cpp
    Source/Editing.cpp
    Source/Looks.cpp
    Source/AudioMix.cpp
    Source/Presets.cpp
)

set(ISFX_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_APPLICATION_NAME_STRING="InSequence FX"
    __STDC_CONSTANT_MACROS
)

set(ISFX_LIBS
    isfx_ffmpeg
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_audio_processors
)

option(ISFX_TESTS_ONLY "Build only the headless test target" OFF)
if(NOT ISFX_TESTS_ONLY)
    juce_add_gui_app(InSequenceFX
        PRODUCT_NAME "InSequence FX"
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/insequence-icon.png"
        COMPANY_NAME "HumHouse"
        BUNDLE_ID "com.humhouse.insequencefx"
        VERSION ${PROJECT_VERSION}
        HARDENED_RUNTIME_ENABLED FALSE
        DOCUMENT_EXTENSIONS isfx
    )
    target_sources(InSequenceFX PRIVATE
        ${ISFX_CORE_SOURCES}
        Source/Renderer.cpp
        Source/Exporter.cpp
        Source/Playback.cpp
        Source/Plugins.cpp
        Source/Theme.cpp
        Source/Timeline.cpp
        Source/Panels.cpp
        Source/MainComponent.cpp
        Source/Main.cpp
    )
    juce_add_binary_data(ISFXData SOURCES installer/LICENSE.txt assets/insequence-icon.png)
    target_compile_definitions(InSequenceFX PRIVATE ${ISFX_DEFS} JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_PLUGINHOST_LADSPA=0)
    if(MSVC)
        target_compile_options(InSequenceFX PRIVATE /Zi)
        target_link_options(InSequenceFX PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    endif()
    target_link_libraries(InSequenceFX PRIVATE ${ISFX_LIBS} ISFXData juce::juce_opengl
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(APPLE)
        set_target_properties(InSequenceFX PROPERTIES BUILD_RPATH "@executable_path/../Frameworks" INSTALL_RPATH "@executable_path/../Frameworks")
        target_link_options(InSequenceFX PRIVATE "-Wl,-rpath,@executable_path/../Frameworks")
    elseif(UNIX)
        target_link_options(InSequenceFX PRIVATE "-Wl,-rpath,$ORIGIN/lib")
    endif()
endif()

# --- Headless tests: timing, analysis, sync, chop, editing, project I/O, media ----
enable_testing()
juce_add_console_app(ISFXTests PRODUCT_NAME "ISFXTests")
target_sources(ISFXTests PRIVATE ${ISFX_CORE_SOURCES} Source/Renderer.cpp Source/Exporter.cpp tests/Tests.cpp)
target_compile_definitions(ISFXTests PRIVATE ${ISFX_DEFS} ISFX_HEADLESS=1)
target_link_libraries(ISFXTests PRIVATE ${ISFX_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME ISFXTests COMMAND ISFXTests)
