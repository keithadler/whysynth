cmake_minimum_required(VERSION 3.21)

project(zedsynth
    VERSION 3.0.0
    DESCRIPTION "Versatile multi-mode software synthesizer"
    HOMEPAGE_URL "https://github.com/keithadler/zedsynth"
    LANGUAGES C)

# With MSVC, link the C runtime statically: a user's machine need not have the Visual C++
# redistributable, just as the MinGW build must not need libwinpthread-1.dll.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(ZEDSYNTH_BUILD_CLAP  "Build the CLAP plugin" ON)
option(ZEDSYNTH_BUILD_LV2   "Build the LV2 plugin" ON)
option(ZEDSYNTH_BUILD_DSSI  "Build the legacy DSSI plugin and GTK2 editor when their libraries are installed" ON)
option(ZEDSYNTH_BUILD_TOOLS "Build zedsynth-render" ON)
option(ZEDSYNTH_BUILD_AUV2  "Build an Audio Unit (macOS) from the CLAP through clap-wrapper" ON)
option(ZEDSYNTH_BUILD_STANDALONE "Build a standalone application from the CLAP through clap-wrapper" ON)
option(ZEDSYNTH_BUILD_TESTS "Build the tests" ON)

include(FetchContent)
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)

find_package(Threads REQUIRED)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(ZEDSYNTH_C_FLAGS -Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-but-set-variable)
else()
    set(ZEDSYNTH_C_FLAGS)
endif()

# ---- the engine ----

set(ZEDSYNTH_CORE_SOURCES
    src/agran_oscillator.c
    src/agran_tables.c
    src/common_data.c
    src/dssp_event.c
    src/effects.c
    src/effect_reverb.c
    src/effect_screverb.c
    src/minblep_tables.c
    src/padsynth.c
    src/patch_tables.c
    src/sampleset.c
    src/wave_data.c
    src/wave_tables.c
    src/zedsynth_data.c
    src/zedsynth_ports.c
    src/zedsynth_voice.c
    src/zedsynth_voice_render.c
    src/zedsynth_core.c
    src/zedsynth_names.c
    src/yfft.c
    third_party/kissfft/kiss_fft.c
    third_party/kissfft/kiss_fftr.c)

add_library(zedsynth_core OBJECT ${ZEDSYNTH_CORE_SOURCES})
target_include_directories(zedsynth_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/kissfft)
target_compile_definitions(zedsynth_core PUBLIC Y_PLUGIN=1 VERSION="${PROJECT_VERSION}" kiss_fft_scalar=float)
target_compile_options(zedsynth_core PRIVATE ${ZEDSYNTH_C_FLAGS})
if(WIN32)
    target_compile_definitions(zedsynth_core PUBLIC _USE_MATH_DEFINES)
endif()

add_library(zedsynth_engine STATIC src/zedsynth_engine.c $<TARGET_OBJECTS:zedsynth_core>)
target_include_directories(zedsynth_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/kissfft)
target_compile_definitions(zedsynth_engine PUBLIC Y_PLUGIN=1 VERSION="${PROJECT_VERSION}" kiss_fft_scalar=float)
target_compile_options(zedsynth_engine PRIVATE ${ZEDSYNTH_C_FLAGS})
target_link_libraries(zedsynth_engine PUBLIC Threads::Threads)
if(MINGW)
    # MinGW's pthreads come from libwinpthread-1.dll, which a Windows machine only has when MinGW is
    # installed. Every plugin and tool takes the engine, so link the runtime into each binary here and
    # the shipped zip stands on its own (2.0.0 failed with "libwinpthread-1.dll was not found").
    target_link_options(zedsynth_engine PUBLIC -static)
endif()
if(NOT MSVC)
    target_link_libraries(zedsynth_engine PUBLIC m)
endif()
if(WIN32)
    target_compile_definitions(zedsynth_engine PUBLIC _USE_MATH_DEFINES)
endif()

# ---- CLAP ----

if(ZEDSYNTH_BUILD_CLAP)
    FetchContent_Declare(clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG        1.2.10
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(clap)

    add_library(zedsynth_clap MODULE plugins/clap/zedsynth_clap.c)
    target_link_libraries(zedsynth_clap PRIVATE zedsynth_engine clap)
    target_compile_options(zedsynth_clap PRIVATE ${ZEDSYNTH_C_FLAGS})
    set_target_properties(zedsynth_clap PROPERTIES OUTPUT_NAME ZedSynth PREFIX "")
    if(APPLE)
        set_target_properties(zedsynth_clap PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION clap
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/plugins/clap/Info.plist.in
            MACOSX_BUNDLE_BUNDLE_NAME ZedSynth
            MACOSX_BUNDLE_GUI_IDENTIFIER com.github.keithadler.zedsynth
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
            MACOSX_BUNDLE_COPYRIGHT "GPL-2.0-or-later. Sean Bolton and others.")
        install(TARGETS zedsynth_clap DESTINATION ${CMAKE_INSTALL_LIBDIR}/clap)
    else()
        set_target_properties(zedsynth_clap PROPERTIES SUFFIX ".clap")
        install(TARGETS zedsynth_clap LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/clap)
    endif()
endif()

# ---- clap-wrapper: the Audio Unit (macOS) and the standalone application ----
# clap-wrapper links the CLAP statically into an AUv2 component and into a small standalone
# host with its own audio and MIDI I/O (RtAudio and RtMidi); it fetches those and Apple's
# AudioUnitSDK itself. Its Windows standalone shell is C++/WinRT, so on Windows the standalone
# needs MSVC or clang: a MinGW build skips it and still produces the plugins and the tool.

set(ZEDSYNTH_WRAP_AUV2 OFF)
set(ZEDSYNTH_WRAP_STANDALONE OFF)
if(ZEDSYNTH_BUILD_CLAP)
    if(APPLE AND ZEDSYNTH_BUILD_AUV2)
        set(ZEDSYNTH_WRAP_AUV2 ON)
    endif()
    if(ZEDSYNTH_BUILD_STANDALONE)
        if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
            message(STATUS "ZedSynth: standalone skipped, clap-wrapper's Windows standalone needs MSVC or clang")
        else()
            set(ZEDSYNTH_WRAP_STANDALONE ON)
        endif()
    endif()
endif()

if(ZEDSYNTH_WRAP_AUV2 OR ZEDSYNTH_WRAP_STANDALONE)
    enable_language(CXX)
    if(APPLE)
        enable_language(OBJC OBJCXX)
    endif()
    if(MSVC)
        # C++/WinRT under C++17 includes <experimental/coroutine>, which the current MSVC STL
        # rejects outright; with C++20 it uses <coroutine>. clap-wrapper takes the standard from here.
        set(CMAKE_CXX_STANDARD 20)
    else()
        set(CMAKE_CXX_STANDARD 17)
    endif()
    set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
    set(CLAP_WRAPPER_BUILD_AUV2 ${ZEDSYNTH_WRAP_AUV2} CACHE BOOL "" FORCE)
    set(CLAP_WRAPPER_BUILD_STANDALONE ${ZEDSYNTH_WRAP_STANDALONE} CACHE BOOL "" FORCE)
    if(WIN32)
        # DirectSound as well as WASAPI, so Audio/MIDI Settings offers a second API when a
        # device will not open through the first.
        set(RTAUDIO_API_DS ON CACHE BOOL "" FORCE)
    endif()
    # The patch makes the standalone fall back to the first usable output device when RtAudio
    # cannot name a default (on Windows: "RtApi::getDeviceInfo: deviceId argument not found"),
    # and keep running without sound when there is none. Offered upstream; drop it when a
    # clap-wrapper release carries it.
    FetchContent_Declare(clap-wrapper
        GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
        GIT_TAG        v0.16.0
        GIT_SHALLOW    TRUE
        PATCH_COMMAND  ${CMAKE_COMMAND} -DPATCH=${CMAKE_CURRENT_SOURCE_DIR}/cmake/clap-wrapper-v0.16.0-standalone-default-audio.patch
                       -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyPatch.cmake
        UPDATE_DISCONNECTED TRUE)
    FetchContent_MakeAvailable(clap-wrapper)

    add_library(zedsynth_clap_impl STATIC plugins/clap/zedsynth_clap.c)
    target_link_libraries(zedsynth_clap_impl PUBLIC zedsynth_engine clap)
    target_include_directories(zedsynth_clap_impl PUBLIC plugins/clap)
    target_compile_definitions(zedsynth_clap_impl PRIVATE ZEDSYNTH_CLAP_NO_ENTRY=1)
    target_compile_options(zedsynth_clap_impl PRIVATE ${ZEDSYNTH_C_FLAGS})

    set(ZEDSYNTH_WRAP_FORMATS CLAP)
    if(ZEDSYNTH_WRAP_AUV2)
        list(APPEND ZEDSYNTH_WRAP_FORMATS AUV2)
    endif()
    set(ZEDSYNTH_WRAP_STANDALONE_ARGS)
    if(ZEDSYNTH_WRAP_STANDALONE)
        # target postfix, output name, CLAP id
        set(ZEDSYNTH_WRAP_STANDALONE_ARGS STANDALONE_CONFIGURATIONS standalone ZedSynth com.github.keithadler.zedsynth)
    endif()

    # Outputs land in build/wrapped: ZedSynth.component (macOS), and the standalone as
    # ZedSynth.app (macOS), ZedSynth (Linux) or Standalone-zedsynth_wrapped_standalone/ZedSynth.exe (Windows).
    make_clapfirst_plugins(
        TARGET_NAME zedsynth_wrapped
        IMPL_TARGET zedsynth_clap_impl
        OUTPUT_NAME "ZedSynth"
        ENTRY_SOURCE "plugins/clap/zedsynth_clap_entry.cpp"
        BUNDLE_IDENTIFIER "com.github.keithadler.zedsynth.au"
        BUNDLE_VERSION ${PROJECT_VERSION}
        COPY_AFTER_BUILD FALSE
        PLUGIN_FORMATS ${ZEDSYNTH_WRAP_FORMATS}
        ${ZEDSYNTH_WRAP_STANDALONE_ARGS}
        ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/wrapped
        AUV2_MANUFACTURER_NAME "Keith Adler"
        AUV2_MANUFACTURER_CODE "KAdl"
        AUV2_SUBTYPE_CODE "WhyS"
        AUV2_INSTRUMENT_TYPE "aumu")
endif()

# ---- LV2 ----

if(ZEDSYNTH_BUILD_LV2)
    find_path(LV2_INCLUDE_DIR lv2/core/lv2.h)
    if(NOT LV2_INCLUDE_DIR)
        FetchContent_Declare(lv2
            GIT_REPOSITORY https://github.com/lv2/lv2.git
            GIT_TAG        v1.18.10
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(lv2)
        set(LV2_INCLUDE_DIR ${lv2_SOURCE_DIR}/include)
    endif()

    if(WIN32)
        set(ZEDSYNTH_LV2_SUFFIX ".dll")
    elseif(APPLE)
        set(ZEDSYNTH_LV2_SUFFIX ".dylib")
    else()
        set(ZEDSYNTH_LV2_SUFFIX ".so")
    endif()
    set(ZEDSYNTH_LV2_BUNDLE_DIR ${CMAKE_BINARY_DIR}/lv2/zedsynth.lv2)

    add_library(zedsynth_lv2 MODULE plugins/lv2/zedsynth_lv2.c)
    target_link_libraries(zedsynth_lv2 PRIVATE zedsynth_engine)
    target_include_directories(zedsynth_lv2 PRIVATE ${LV2_INCLUDE_DIR} plugins/lv2)
    target_compile_options(zedsynth_lv2 PRIVATE ${ZEDSYNTH_C_FLAGS})
    set_target_properties(zedsynth_lv2 PROPERTIES
        OUTPUT_NAME zedsynth
        PREFIX ""
        SUFFIX "${ZEDSYNTH_LV2_SUFFIX}"
        LIBRARY_OUTPUT_DIRECTORY ${ZEDSYNTH_LV2_BUNDLE_DIR})

    # the port description and factory presets are generated from the engine
    add_executable(zedsynth_lv2_gen tools/zedsynth_lv2_gen.c)
    target_link_libraries(zedsynth_lv2_gen PRIVATE zedsynth_engine)
    target_include_directories(zedsynth_lv2_gen PRIVATE plugins/lv2)
    set_target_properties(zedsynth_lv2_gen PROPERTIES OUTPUT_NAME zedsynth-lv2-gen)
    add_custom_command(
        OUTPUT ${ZEDSYNTH_LV2_BUNDLE_DIR}/zedsynth.ttl ${ZEDSYNTH_LV2_BUNDLE_DIR}/presets.ttl ${ZEDSYNTH_LV2_BUNDLE_DIR}/manifest.ttl
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ZEDSYNTH_LV2_BUNDLE_DIR}
        COMMAND zedsynth_lv2_gen ttl ${ZEDSYNTH_LV2_BUNDLE_DIR}/zedsynth.ttl
        COMMAND zedsynth_lv2_gen presets ${ZEDSYNTH_LV2_BUNDLE_DIR}/presets.ttl
        COMMAND zedsynth_lv2_gen manifest ${ZEDSYNTH_LV2_BUNDLE_DIR}/manifest.ttl ${ZEDSYNTH_LV2_SUFFIX}
        DEPENDS zedsynth_lv2_gen
        COMMENT "Generating the LV2 port description and factory presets"
        VERBATIM)
    add_custom_target(zedsynth_lv2_ttl ALL DEPENDS ${ZEDSYNTH_LV2_BUNDLE_DIR}/zedsynth.ttl ${ZEDSYNTH_LV2_BUNDLE_DIR}/presets.ttl ${ZEDSYNTH_LV2_BUNDLE_DIR}/manifest.ttl)
    add_dependencies(zedsynth_lv2 zedsynth_lv2_ttl)

    install(TARGETS zedsynth_lv2 LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/lv2/zedsynth.lv2)
    install(FILES ${ZEDSYNTH_LV2_BUNDLE_DIR}/manifest.ttl ${ZEDSYNTH_LV2_BUNDLE_DIR}/zedsynth.ttl
                  ${ZEDSYNTH_LV2_BUNDLE_DIR}/presets.ttl
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/lv2/zedsynth.lv2)
endif()

# ---- legacy DSSI plugin and GTK2 editor (Linux, when the libraries exist) ----

if(ZEDSYNTH_BUILD_DSSI)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(DSSI QUIET dssi>=0.9 alsa)
        pkg_check_modules(ZEDSYNTH_GTK QUIET gtk+-2.0>=2.4 liblo>=0.12 alsa)
    endif()
    if(DSSI_FOUND)
        add_library(zedsynth_dssi MODULE src/dssp_synth.c $<TARGET_OBJECTS:zedsynth_core>)
        target_include_directories(zedsynth_dssi PRIVATE src third_party/kissfft ${DSSI_INCLUDE_DIRS})
        target_compile_definitions(zedsynth_dssi PRIVATE Y_PLUGIN=1 VERSION="${PROJECT_VERSION}" kiss_fft_scalar=float)
        target_compile_options(zedsynth_dssi PRIVATE ${ZEDSYNTH_C_FLAGS} ${DSSI_CFLAGS_OTHER})
        target_link_libraries(zedsynth_dssi PRIVATE ${DSSI_LIBRARIES} Threads::Threads m)
        set_target_properties(zedsynth_dssi PROPERTIES OUTPUT_NAME zedsynth PREFIX "" SUFFIX ".so"
                              C_VISIBILITY_PRESET default)
        install(TARGETS zedsynth_dssi LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/dssi)
        message(STATUS "ZedSynth: building the legacy DSSI plugin")
    else()
        message(STATUS "ZedSynth: DSSI/ALSA not found, skipping the legacy DSSI plugin")
    endif()
    if(DSSI_FOUND AND ZEDSYNTH_GTK_FOUND)
        add_executable(zedsynth_gtk
            src/gui_main.c src/agran_tables.c src/common_data.c src/gtkknob.c src/gui_callbacks.c
            src/gui_data.c src/gui_images.c src/gui_interface.c src/patch_tables.c src/wave_tables.c
            src/zedsynth_ports.c)
        target_include_directories(zedsynth_gtk PRIVATE src ${ZEDSYNTH_GTK_INCLUDE_DIRS} ${DSSI_INCLUDE_DIRS})
        target_compile_definitions(zedsynth_gtk PRIVATE Y_GUI=1 VERSION="${PROJECT_VERSION}"
                                   THREAD_LOCALE_LOCALE_H=1)
        target_compile_options(zedsynth_gtk PRIVATE ${ZEDSYNTH_GTK_CFLAGS_OTHER} -Wno-deprecated-declarations)
        target_link_libraries(zedsynth_gtk PRIVATE ${ZEDSYNTH_GTK_LIBRARIES} m)
        set_target_properties(zedsynth_gtk PROPERTIES OUTPUT_NAME ZedSynth_gtk)
        install(TARGETS zedsynth_gtk RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/dssi/zedsynth)
        message(STATUS "ZedSynth: building the legacy GTK2 editor")
    endif()
endif()

if(WIN32)
    # The CLAP, the LV2 and clap-wrapper's CLAP are all DLLs named ZedSynth or zedsynth. MSVC writes an
    # import library and .exp next to each, so give each its own directory or the links collide.
    foreach(t zedsynth_clap zedsynth_lv2 zedsynth_wrapped_clap)
        if(TARGET ${t})
            set_target_properties(${t} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/implib/${t})
        endif()
    endforeach()
endif()

# ---- command line renderer ----

if(ZEDSYNTH_BUILD_TOOLS)
    add_executable(zedsynth_render tools/zedsynth_render.c)
    target_link_libraries(zedsynth_render PRIVATE zedsynth_engine)
    target_compile_options(zedsynth_render PRIVATE ${ZEDSYNTH_C_FLAGS})
    set_target_properties(zedsynth_render PROPERTIES OUTPUT_NAME zedsynth-render)
    install(TARGETS zedsynth_render RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ---- tests ----

if(ZEDSYNTH_BUILD_TESTS)
    enable_testing()

    add_executable(test_engine tests/test_engine.c)
    target_link_libraries(test_engine PRIVATE zedsynth_engine)
    target_compile_options(test_engine PRIVATE ${ZEDSYNTH_C_FLAGS})
    add_test(NAME engine COMMAND test_engine ${CMAKE_CURRENT_SOURCE_DIR}/extra)

    if(ZEDSYNTH_BUILD_CLAP)
        add_executable(test_clap tests/test_clap.c)
        target_link_libraries(test_clap PRIVATE clap ${CMAKE_DL_LIBS})
        if(NOT MSVC)
            target_link_libraries(test_clap PRIVATE m)
        endif()
        target_compile_options(test_clap PRIVATE ${ZEDSYNTH_C_FLAGS})
        add_test(NAME clap COMMAND test_clap $<TARGET_FILE:zedsynth_clap> ${CMAKE_CURRENT_SOURCE_DIR}/extra)
    endif()

    if(ZEDSYNTH_BUILD_LV2)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LILV QUIET lilv-0)
        endif()
        if(LILV_FOUND)
            add_executable(test_lv2 tests/test_lv2.c)
            target_include_directories(test_lv2 PRIVATE ${LILV_INCLUDE_DIRS} ${LV2_INCLUDE_DIR} plugins/lv2)
            target_link_libraries(test_lv2 PRIVATE ${LILV_LINK_LIBRARIES} m)
            target_compile_options(test_lv2 PRIVATE ${ZEDSYNTH_C_FLAGS} ${LILV_CFLAGS_OTHER})
            add_test(NAME lv2 COMMAND test_lv2 ${ZEDSYNTH_LV2_BUNDLE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/extra)
            message(STATUS "ZedSynth: lilv found, the LV2 plugin will be tested")
        else()
            message(STATUS "ZedSynth: lilv not found, the LV2 plugin will be built but not tested")
        endif()
    endif()

    if(ZEDSYNTH_BUILD_TOOLS)
        add_test(NAME render_tool
            COMMAND zedsynth_render --program 1 --note 60 --note 64 --hold 0.5 --seconds 1 --wait 0.2
                    --out ${CMAKE_BINARY_DIR}/render_test.wav)
        add_test(NAME render_list COMMAND zedsynth_render --list)
    endif()
endif()

# ---- packaging ----

install(FILES extra/current_default_patches.WhySynth extra/more_K4_interpretations.WhySynth
              extra/version_20120903_patches.WhySynth extra/COPYING-patches
        DESTINATION ${CMAKE_INSTALL_DATADIR}/zedsynth/patches)
