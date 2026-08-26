cmake_minimum_required(VERSION 3.15)
project(Spasis VERSION 0.1.0 LANGUAGES C CXX)

#-----------------------------------------------------------------------------
# Architecture. Must sit immediately after project() and before any target
# exists: CMake latches the architecture list when the first target is created,
# and setting it later is silently ignored -- the build succeeds and produces a
# single-architecture binary. Verify with `lipo`, never with the build log.
#-----------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()
if(MSVC)
    add_compile_options(/utf-8)
endif()
if(WIN32)
    # windows.h defines min/max as macros, which breaks std::min/std::max.
    add_compile_definitions(NOMINMAX)
endif()

option(SPASIS_BUILD_TOOLS "Build the offline harness" ON)

#-----------------------------------------------------------------------------
# The FFGL SDK
#-----------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ffgl/CMakeLists.txt")
    message(FATAL_ERROR
        "FFGL SDK not found. Run: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/miniaudio/miniaudio.h")
    message(FATAL_ERROR
        "miniaudio not found. Run: git submodule update --init --recursive")
endif()

set(FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/ffgl EXCLUDE_FROM_ALL)

#-----------------------------------------------------------------------------
# The shared core.
#
# OBJECT and not STATIC on purpose -- see the comment at the top of any file in
# source/sources/. Note what is NOT in here: the four registrations.
#-----------------------------------------------------------------------------
add_library(spasis_core OBJECT
    source/Spasis.h                 source/Spasis.cpp
    source/Diag.h                   source/Diag.cpp
    source/StoatworksAbout.h        source/StoatworksAboutParams.h
    source/StoatworksAboutLinks.h

    source/analysis/Frame.h         source/analysis/Layout.cpp
    source/analysis/FFT.h           source/analysis/FFT.cpp
    source/analysis/Analyser.h      source/analysis/Analyser.cpp

    source/audio/Ring.h
    source/audio/Capture.h          source/audio/Capture.cpp

    source/render/View.h
    source/render/Builder.h         source/render/Builder.cpp
    source/render/Canvas.h          source/render/Canvas.cpp
    source/render/ScopeBuffer.h     source/render/ScopeBuffer.cpp
    source/render/GLState.h
)

target_compile_definitions(spasis_core PUBLIC SPASIS_VERSION="v${PROJECT_VERSION}")
target_include_directories(spasis_core PUBLIC
    source
    external/miniaudio
)
target_link_libraries(spasis_core PUBLIC ffgl::sdk)
set_target_properties(spasis_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

#-----------------------------------------------------------------------------
# Platform audio. miniaudio picks its own backend, but it does not link the
# frameworks for us, and on Linux it dlopen()s ALSA/PulseAudio at runtime --
# so the only hard link there is the dynamic loader and pthreads.
#-----------------------------------------------------------------------------
if(APPLE)
    target_link_libraries(spasis_core PUBLIC
        "-framework CoreAudio" "-framework AudioToolbox" "-framework CoreFoundation")
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(spasis_core PUBLIC ${CMAKE_DL_LIBS} Threads::Threads m)
endif()

if(NOT APPLE)
    find_package(GLEW REQUIRED)   # arrives through vcpkg.json on Windows
    target_link_libraries(spasis_core PUBLIC GLEW::GLEW)
endif()

#-----------------------------------------------------------------------------
# The four plugins. FFGL resolves one plugMain per binary, so four sources are
# four bundles.
#-----------------------------------------------------------------------------
function(spasis_add_plugin target entry bundle_name identifier)
    add_library(${target} MODULE ${entry})
    target_link_libraries(${target} PRIVATE spasis_core)

    if(APPLE)
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION "bundle"
            OUTPUT_NAME "${bundle_name}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${bundle_name}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${identifier}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        )
        # FFGL hosts resolve plugMain by symbol name, so keep default visibility.
        target_link_libraries(${target} PRIVATE
            "-framework OpenGL" "-framework Carbon" "-framework AppKit")
    else()
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${bundle_name}")
    endif()
endfunction()

spasis_add_plugin(SpasisField   source/sources/SpasisField.cpp
    "Spasis Field"   "com.stoatworks.ffgl.spasis.field")
spasis_add_plugin(SpasisRose    source/sources/SpasisRose.cpp
    "Spasis Rose"    "com.stoatworks.ffgl.spasis.rose")
spasis_add_plugin(SpasisWidth   source/sources/SpasisWidth.cpp
    "Spasis Width"   "com.stoatworks.ffgl.spasis.width")
spasis_add_plugin(SpasisBalance source/sources/SpasisBalance.cpp
    "Spasis Balance" "com.stoatworks.ffgl.spasis.balance")

#-----------------------------------------------------------------------------
# The offline harness.
#
# `sptest` links the same OBJECT library the bundles do, so what it measures is
# what ships. The invariant half needs no GL context at all, which is why the
# geometry mapping is on the CPU -- see render/View.h.
#-----------------------------------------------------------------------------
if(SPASIS_BUILD_TOOLS)
    add_executable(sptest tools/sptest/Invariants.cpp tools/sptest/Params.cpp tools/sptest/Devices.cpp)
    if(APPLE)
        target_sources(sptest PRIVATE tools/sptest/Shaders.cpp tools/sptest/Render.cpp)
        # zlib ships with the OS, which is why the PNG writer is fifty lines in
        # the harness rather than a vendored dependency.
        target_link_libraries(sptest PRIVATE z)
    endif()
    target_link_libraries(sptest PRIVATE spasis_core)
    if(APPLE)
        target_link_libraries(sptest PRIVATE "-framework OpenGL" "-framework AppKit")
    endif()
endif()

#-----------------------------------------------------------------------------
# Install: straight into Resolume's plugin folder.
#-----------------------------------------------------------------------------
if(APPLE AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/Documents/Resolume Arena/Extra Effects"
        CACHE PATH "" FORCE)
endif()

install(TARGETS SpasisField SpasisRose SpasisWidth SpasisBalance
    LIBRARY DESTINATION "."
    BUNDLE  DESTINATION "."
)
