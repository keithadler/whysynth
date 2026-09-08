cmake_minimum_required(VERSION 3.21)

project(whysynth
    VERSION 2.0.0
    DESCRIPTION "Versatile multi-mode software synthesizer"
    HOMEPAGE_URL "https://github.com/keithadler/whysynth"
    LANGUAGES C)

option(WHYSYNTH_BUILD_CLAP  "Build the CLAP plugin" ON)
option(WHYSYNTH_BUILD_LV2   "Build the LV2 plugin" ON)
option(WHYSYNTH_BUILD_DSSI  "Build the legacy DSSI plugin and GTK2 editor when their libraries are installed" ON)
option(WHYSYNTH_BUILD_TOOLS "Build whysynth-render" ON)
option(WHYSYNTH_BUILD_AUV2  "Build an Audio Unit (macOS) from the CLAP through clap-wrapper" ON)
option(WHYSYNTH_BUILD_TESTS "Build the tests" ON)

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
    set(WHYSYNTH_C_FLAGS -Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-but-set-variable)
else()
    set(WHYSYNTH_C_FLAGS)
endif()

# ---- the engine ----

set(WHYSYNTH_CORE_SOURCES
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
    src/whysynth_data.c
    src/whysynth_ports.c
    src/whysynth_voice.c
    src/whysynth_voice_render.c
    src/whysynth_core.c
    src/whysynth_names.c
    src/yfft.c
    third_party/kissfft/kiss_fft.c
    third_party/kissfft/kiss_fftr.c)

add_library(whysynth_core OBJECT ${WHYSYNTH_CORE_SOURCES})
target_include_directories(whysynth_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/kissfft)
target_compile_definitions(whysynth_core PUBLIC Y_PLUGIN=1 VERSION="${PROJECT_VERSION}" kiss_fft_scalar=float)
target_compile_options(whysynth_core PRIVATE ${WHYSYNTH_C_FLAGS})
if(WIN32)
    target_compile_definitions(whysynth_core PUBLIC _USE_MATH_DEFINES)
endif()

add_library(whysynth_engine STATIC src/whysynth_engine.c $<TARGET_OBJECTS:whysynth_core>)
target_include_directories(whysynth_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/kissfft)
target_compile_definitions(whysynth_engine PUBLIC Y_PLUGIN=1 VERSION="${PROJECT_VERSION}" kiss_fft_scalar=float)
target_compile_options(whysynth_engine PRIVATE ${WHYSYNTH_C_FLAGS})
target_link_libraries(whysynth_engine PUBLIC Threads::Threads)
if(NOT MSVC)
    target_link_libraries(whysynth_engine PUBLIC m)
endif()
if(WIN32)
    target_compile_definitions(whysynth_engine PUBLIC _USE_MATH_DEFINES)
endif()

# ---- CLAP ----

if(WHYSYNTH_BUILD_CLAP)
    FetchContent_Declare(clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG        1.2.10
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(clap)

    add_library(whysynth_clap MODULE plugins/clap/whysynth_clap.c)
    target_link_libraries(whysynth_clap PRIVATE whysynth_engine clap)
    target_compile_options(whysynth_clap PRIVATE ${WHYSYNTH_C_FLAGS})
    set_target_properties(whysynth_clap PROPERTIES OUTPUT_NAME WhySynth PREFIX "")
    if(APPLE)
        set_target_properties(whysynth_clap PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION clap
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/plugins/clap/Info.plist.in
            MACOSX_BUNDLE_BUNDLE_NAME WhySynth
            MACOSX_BUNDLE_GUI_IDENTIFIER com.github.keithadler.whysynth
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
            MACOSX_BUNDLE_COPYRIGHT "GPL-2.0-or-later. Sean Bolton and others.")
        install(TARGETS whysynth_clap DESTINATION ${CMAKE_INSTALL_LIBDIR}/clap)
    else()
        set_target_properties(whysynth_clap PROPERTIES SUFFIX ".clap")
        install(TARGETS whysynth_clap LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/clap)
    endif()
endif()

# ---- Audio Unit v2, for Logic and GarageBand (macOS) ----
# clap-wrapper links the CLAP statically into an AUv2 component. It fetches
# Apple's AudioUnitSDK itself.

if(APPLE AND WHYSYNTH_BUILD_CLAP AND WHYSYNTH_BUILD_AUV2)
    enable_language(CXX OBJC OBJCXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
    set(CLAP_WRAPPER_BUILD_AUV2 ON CACHE BOOL "" FORCE)
    FetchContent_Declare(clap-wrapper
        GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
        GIT_TAG        v0.16.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(clap-wrapper)

    add_library(whysynth_clap_impl STATIC plugins/clap/whysynth_clap.c)
    target_link_libraries(whysynth_clap_impl PUBLIC whysynth_engine clap)
    target_include_directories(whysynth_clap_impl PUBLIC plugins/clap)
    target_compile_definitions(whysynth_clap_impl PRIVATE WHYSYNTH_CLAP_NO_ENTRY=1)
    target_compile_options(whysynth_clap_impl PRIVATE ${WHYSYNTH_C_FLAGS})

    make_clapfirst_plugins(
        TARGET_NAME whysynth_au
        IMPL_TARGET whysynth_clap_impl
        OUTPUT_NAME "WhySynth"
        ENTRY_SOURCE "plugins/clap/whysynth_clap_entry.cpp"
        BUNDLE_IDENTIFIER "com.github.keithadler.whysynth.au"
        BUNDLE_VERSION ${PROJECT_VERSION}
        COPY_AFTER_BUILD FALSE
        PLUGIN_FORMATS CLAP AUV2
        ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/auv2
        AUV2_MANUFACTURER_NAME "Keith Adler"
        AUV2_MANUFACTURER_CODE "KAdl"
        AUV2_SUBTYPE_CODE "WhyS"
        AUV2_INSTRUMENT_TYPE "aumu")
endif()

# ---- LV2 ----

if(WHYSYNTH_BUILD_LV2)
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
        set(WHYSYNTH_LV2_SUFFIX ".dll")
    elseif(APPLE)
        set(WHYSYNTH_LV2_SUFFIX ".dylib")
    else()
        set(WHYSYNTH_LV2_SUFFIX ".so")
    endif()
    set(WHYSYNTH_LV2_BUNDLE_DIR ${CMAKE_BINARY_DIR}/lv2/whysynth.lv2)

    add_library(whysynth_lv2 MODULE plugins/lv2/whysynth_lv2.c)
    target_link_libraries(whysynth_lv2 PRIVATE whysynth_engine)
    target_include_directories(whysynth_lv2 PRIVATE ${LV2_INCLUDE_DIR} plugins/lv2)
    target_compile_options(whysynth_lv2 PRIVATE ${WHYSYNTH_C_FLAGS})
    set_target_properties(whysynth_lv2 PROPERTIES
        OUTPUT_NAME whysynth
        PREFIX ""
        SUFFIX "${WHYSYNTH_LV2_SUFFIX}"
        LIBRARY_OUTPUT_DIRECTORY ${WHYSYNTH_LV2_BUNDLE_DIR})

    # the port description and factory presets are generated from the engine
    add_executable(whysynth_lv2_gen tools/whysynth_lv2_gen.c)
    target_link_libraries(whysynth_lv2_gen PRIVATE whysynth_engine)
    target_include_directories(whysynth_lv2_gen PRIVATE plugins/lv2)
    set_target_properties(whysynth_lv2_gen PROPERTIES OUTPUT_NAME whysynth-lv2-gen)
    add_custom_command(
        OUTPUT ${WHYSYNTH_LV2_BUNDLE_DIR}/whysynth.ttl ${WHYSYNTH_LV2_BUNDLE_DIR}/presets.ttl ${WHYSYNTH_LV2_BUNDLE_DIR}/manifest.ttl
        COMMAND ${CMAKE_COMMAND} -E make_directory ${WHYSYNTH_LV2_BUNDLE_DIR}
        COMMAND whysynth_lv2_gen ttl ${WHYSYNTH_LV2_BUNDLE_DIR}/whysynth.ttl
        COMMAND whysynth_lv2_gen presets ${WHYSYNTH_LV2_BUNDLE_DIR}/presets.ttl
        COMMAND whysynth_lv2_gen manifest ${WHYSYNTH_LV2_BUNDLE_DIR}/manifest.ttl ${WHYSYNTH_LV2_SUFFIX}
        DEPENDS whysynth_lv2_gen
        COMMENT "Generating the LV2 port description and factory presets"
        VERBATIM)
    add_custom_target(whysynth_lv2_ttl ALL DEPENDS ${WHYSYNTH_LV2_BUNDLE_DIR}/whysynth.ttl ${WHYSYNTH_LV2_BUNDLE_DIR}/presets.ttl ${WHYSYNTH_LV2_BUNDLE_DIR}/manifest.ttl)
    add_dependencies(whysynth_lv2 whysynth_lv2_ttl)

    install(TARGETS whysynth_lv2 LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/lv2/whysynth.lv2)
    install(FILES ${WHYSYNTH_LV2_BUNDLE_DIR}/manifest.ttl ${WHYSYNTH_LV2_BUNDLE_DIR}/whysynth.ttl
                  ${WHYSYNTH_LV2_BUNDLE_DIR}/presets.ttl
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/lv2/whysynth.lv2)
endif()

# ---- legacy DSSI plugin and GTK2 editor (Linux, when the libraries exist) ----

if(WHYSYNTH_BUILD_DSSI)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(DSSI QUIET dssi>=0.9 alsa)
        pkg_check_modules(WHYSYNTH_GTK QUIET gtk+-2.0>=2.4 liblo>=0.12 alsa)
    endif()
    if(DSSI_FOUND)
        add_library(whysynth_dssi MODULE src/dssp_synth.c $<TARGET_OBJECTS:whysynth_core>)
        target_include_directories(whysynth_dssi PRIVATE src third_party/kissfft ${DSSI_INCLUDE_DIRS})
        target_compile_definitions(whysynth_dssi PRIVATE Y_PLUGIN=1 VERSION="${PROJECT_VERSION}" kiss_fft_scalar=float)
        target_compile_options(whysynth_dssi PRIVATE ${WHYSYNTH_C_FLAGS} ${DSSI_CFLAGS_OTHER})
        target_link_libraries(whysynth_dssi PRIVATE ${DSSI_LIBRARIES} Threads::Threads m)
        set_target_properties(whysynth_dssi PROPERTIES OUTPUT_NAME whysynth PREFIX "" SUFFIX ".so"
                              C_VISIBILITY_PRESET default)
        install(TARGETS whysynth_dssi LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/dssi)
        message(STATUS "WhySynth: building the legacy DSSI plugin")
    else()
        message(STATUS "WhySynth: DSSI/ALSA not found, skipping the legacy DSSI plugin")
    endif()
    if(DSSI_FOUND AND WHYSYNTH_GTK_FOUND)
        add_executable(whysynth_gtk
            src/gui_main.c src/agran_tables.c src/common_data.c src/gtkknob.c src/gui_callbacks.c
            src/gui_data.c src/gui_images.c src/gui_interface.c src/patch_tables.c src/wave_tables.c
            src/whysynth_ports.c)
        target_include_directories(whysynth_gtk PRIVATE src ${WHYSYNTH_GTK_INCLUDE_DIRS} ${DSSI_INCLUDE_DIRS})
        target_compile_definitions(whysynth_gtk PRIVATE Y_GUI=1 VERSION="${PROJECT_VERSION}"
                                   THREAD_LOCALE_LOCALE_H=1)
        target_compile_options(whysynth_gtk PRIVATE ${WHYSYNTH_GTK_CFLAGS_OTHER} -Wno-deprecated-declarations)
        target_link_libraries(whysynth_gtk PRIVATE ${WHYSYNTH_GTK_LIBRARIES} m)
        set_target_properties(whysynth_gtk PROPERTIES OUTPUT_NAME WhySynth_gtk)
        install(TARGETS whysynth_gtk RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/dssi/whysynth)
        message(STATUS "WhySynth: building the legacy GTK2 editor")
    endif()
endif()

# ---- command line renderer ----

if(WHYSYNTH_BUILD_TOOLS)
    add_executable(whysynth_render tools/whysynth_render.c)
    target_link_libraries(whysynth_render PRIVATE whysynth_engine)
    target_compile_options(whysynth_render PRIVATE ${WHYSYNTH_C_FLAGS})
    set_target_properties(whysynth_render PROPERTIES OUTPUT_NAME whysynth-render)
    install(TARGETS whysynth_render RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ---- tests ----

if(WHYSYNTH_BUILD_TESTS)
    enable_testing()

    add_executable(test_engine tests/test_engine.c)
    target_link_libraries(test_engine PRIVATE whysynth_engine)
    target_compile_options(test_engine PRIVATE ${WHYSYNTH_C_FLAGS})
    add_test(NAME engine COMMAND test_engine ${CMAKE_CURRENT_SOURCE_DIR}/extra)

    if(WHYSYNTH_BUILD_CLAP)
        add_executable(test_clap tests/test_clap.c)
        target_link_libraries(test_clap PRIVATE clap ${CMAKE_DL_LIBS})
        if(NOT MSVC)
            target_link_libraries(test_clap PRIVATE m)
        endif()
        target_compile_options(test_clap PRIVATE ${WHYSYNTH_C_FLAGS})
        add_test(NAME clap COMMAND test_clap $<TARGET_FILE:whysynth_clap> ${CMAKE_CURRENT_SOURCE_DIR}/extra)
    endif()

    if(WHYSYNTH_BUILD_LV2)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LILV QUIET lilv-0)
        endif()
        if(LILV_FOUND)
            add_executable(test_lv2 tests/test_lv2.c)
            target_include_directories(test_lv2 PRIVATE ${LILV_INCLUDE_DIRS} ${LV2_INCLUDE_DIR} plugins/lv2)
            target_link_libraries(test_lv2 PRIVATE ${LILV_LINK_LIBRARIES} m)
            target_compile_options(test_lv2 PRIVATE ${WHYSYNTH_C_FLAGS} ${LILV_CFLAGS_OTHER})
            add_test(NAME lv2 COMMAND test_lv2 ${WHYSYNTH_LV2_BUNDLE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/extra)
            message(STATUS "WhySynth: lilv found, the LV2 plugin will be tested")
        else()
            message(STATUS "WhySynth: lilv not found, the LV2 plugin will be built but not tested")
        endif()
    endif()

    if(WHYSYNTH_BUILD_TOOLS)
        add_test(NAME render_tool
            COMMAND whysynth_render --program 1 --note 60 --note 64 --hold 0.5 --seconds 1 --wait 0.2
                    --out ${CMAKE_BINARY_DIR}/render_test.wav)
        add_test(NAME render_list COMMAND whysynth_render --list)
    endif()
endif()

# ---- packaging ----

install(FILES extra/current_default_patches.WhySynth extra/more_K4_interpretations.WhySynth
              extra/version_20120903_patches.WhySynth extra/COPYING-patches
        DESTINATION ${CMAKE_INSTALL_DATADIR}/whysynth/patches)
