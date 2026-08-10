cmake_minimum_required(VERSION 3.23.0)

# ---------------------------------------------------------------------------
# Event-Triggered Analysis: three Open Ephys plugins (TriggeredPower,
# TriggeredCoherence, TriggeredAverage) over two statically-linked cores.
#
#   trigger_core   ring buffer, trigger sources, work queue, capture worker,
#                  broadcast-message path, trigger config + monitor windows.
#                  No FFTW, no DSP.
#   spectra_core   FFTW, DPSS, Morlet, accumulators, spectral display widgets.
#                  Layered on trigger_core; used by the two spectral plugins.
# ---------------------------------------------------------------------------

set(PROJECT_SHORT_NAME EventTriggeredAnalysis)

set(PLUGIN_VERSION_MAJOR 0)
set(PLUGIN_VERSION_MINOR 1)
set(PLUGIN_VERSION_PATCH 0)
set(PLUGIN_VERSION "${PLUGIN_VERSION_MAJOR}.${PLUGIN_VERSION_MINOR}.${PLUGIN_VERSION_PATCH}")

# Open Ephys GUI plugin API version this repo targets (see OpenEphysPlugin.h)
set(OE_GUI_API_VERSION 10)
set(OE_GUI_VERSION "1.1.0" CACHE STRING "Open Ephys GUI version")

project(OE_PLUGIN_${PROJECT_SHORT_NAME} VERSION ${PLUGIN_VERSION})

option(BUILD_TESTS "Build the test suite" ON)

# --- Locate the Open Ephys GUI source tree --------------------------------
if (NOT DEFINED GUI_BASE_DIR)
  if (DEFINED ENV{GUI_BASE_DIR})
    set(GUI_BASE_DIR $ENV{GUI_BASE_DIR})
  else()
    set(GUI_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../plugin-GUI)
  endif()
endif()

if (NOT EXISTS ${GUI_BASE_DIR})
  message(FATAL_ERROR "GUI_BASE_DIR does not exist: ${GUI_BASE_DIR}")
endif()

get_filename_component(GUI_BASE_DIR ${GUI_BASE_DIR} ABSOLUTE)

set(CMAKE_SHARED_LIBRARY_PREFIX "")
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  set(LINUX 1)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
  endif()
endif()

set(GUI_COMMONLIB_DIR ${GUI_BASE_DIR}/installed_libs)
set(CONFIGURATION_FOLDER $<$<CONFIG:Debug>:Debug>$<$<NOT:$<CONFIG:Debug>>:Release>)
set(GUI_BIN_DIR ${GUI_BASE_DIR}/Build/${CONFIGURATION_FOLDER})

# Scrape the actual GUI version out of the GUI's own CMakeLists.txt
if(EXISTS "${GUI_BASE_DIR}/CMakeLists.txt")
  file(READ "${GUI_BASE_DIR}/CMakeLists.txt" GUI_CMAKE_CONTENT)
  string(REGEX MATCH "set\\(GUI_VERSION ([^)]+)\\)" _ "${GUI_CMAKE_CONTENT}")
  if(CMAKE_MATCH_1)
    string(STRIP "${CMAKE_MATCH_1}" GUI_VERSION_EXTRACTED)
    string(REPLACE "\"" "" GUI_VERSION_EXTRACTED "${GUI_VERSION_EXTRACTED}")
    set(OE_GUI_VERSION "${GUI_VERSION_EXTRACTED}")
    message(STATUS "Detected Open Ephys GUI version: ${OE_GUI_VERSION}")
  endif()
endif()

# --- Architecture (needed so find_library picks up libs/<os>/lib/<arch>) ---
if (NOT CMAKE_LIBRARY_ARCHITECTURE)
  if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CMAKE_LIBRARY_ARCHITECTURE "x64")
  else()
    set(CMAKE_LIBRARY_ARCHITECTURE "x86")
  endif()
endif()

# FFTW is discovered and shipped by Source/Spectral, the only place that needs
# it. Deliberately not here: a plugin that links trigger_core alone must be
# buildable on a checkout with no FFTW at all, and a top-level find_library()
# with a FATAL_ERROR would make that impossible.

# --- Generated version header ---------------------------------------------
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginVersion.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/PluginVersion.h
  @ONLY)

find_program(CLANG_FORMAT_EXE NAMES "clang-format")
if(CLANG_FORMAT_EXE)
  message(STATUS "clang-format found: ${CLANG_FORMAT_EXE}")
endif()

# ---------------------------------------------------------------------------
# Settings shared by every target in this repo (core, plugins, tests).
# ---------------------------------------------------------------------------
function(oe_apply_common_settings target)
  target_compile_features(${target} PUBLIC cxx_std_23)

  target_compile_definitions(${target} PUBLIC
    OEPLUGIN
    "$<$<PLATFORM_ID:Windows>:JUCE_API=__declspec(dllimport)>"
    $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>
    $<$<PLATFORM_ID:Linux>:JUCE_DISABLE_NATIVE_FILECHOOSERS=1>
    $<$<CONFIG:Debug>:DEBUG=1>
    $<$<CONFIG:Debug>:_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>)

  # PROJECT_BINARY_DIR / PROJECT_SOURCE_DIR (not CMAKE_CURRENT_*) so these stay
  # anchored at the top level when the function is called from a subdirectory.
  target_include_directories(${target} PUBLIC
    ${GUI_BASE_DIR}/JuceLibraryCode
    ${GUI_BASE_DIR}/JuceLibraryCode/modules
    ${GUI_BASE_DIR}/Plugins/Headers
    ${GUI_COMMONLIB_DIR}/include
    ${PROJECT_BINARY_DIR})

  if(MSVC)
    target_compile_options(${target} PRIVATE /W3 /MP /bigobj /external:W0 /external:anglebrackets)
    # /fp:fast is safe here: no NaN/Inf-sensitive logic in the DSP paths.
    target_compile_options(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:/fp:fast>)
  else()
    target_compile_options(${target} PRIVATE -fPIC)
    target_compile_options(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:-O3>)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Turn a source list into an installable Open Ephys plugin.
#
# PLUGIN_CORE is the static core to link: spectra_core for the frequency-domain
# plugins, trigger_core for one that only needs trial windows. Passing it rather
# than hardcoding it is what keeps FFTW out of the averaging plugin.
# ---------------------------------------------------------------------------
function(oe_add_plugin PLUGIN_NAME PLUGIN_CORE)
  set(PLUGIN_SOURCES ${ARGN})

  if(WIN32)
    # PLUGIN_NAME is visible to configure_file as a function-scope variable, so
    # each plugin gets its own InternalName / OriginalFilename.
    configure_file(
      ${PROJECT_SOURCE_DIR}/Resources/Version.rc.in
      ${CMAKE_CURRENT_BINARY_DIR}/${PLUGIN_NAME}_Version.rc
      @ONLY)
    list(APPEND PLUGIN_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/${PLUGIN_NAME}_Version.rc)
  endif()

  if (APPLE)
    add_library(${PLUGIN_NAME} MODULE ${PLUGIN_SOURCES})
  else()
    add_library(${PLUGIN_NAME} SHARED ${PLUGIN_SOURCES})
  endif()

  oe_apply_common_settings(${PLUGIN_NAME})
  target_link_libraries(${PLUGIN_NAME} PRIVATE ${PLUGIN_CORE})

  set_target_properties(${PLUGIN_NAME} PROPERTIES
    VERSION ${PLUGIN_VERSION}
    SOVERSION ${PLUGIN_VERSION_MAJOR})

  if(MSVC)
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${GUI_BIN_DIR}/open-ephys.lib)
    install(TARGETS ${PLUGIN_NAME}
            RUNTIME DESTINATION ${GUI_BIN_DIR}/plugins
            CONFIGURATIONS ${CMAKE_CONFIGURATION_TYPES})
  elseif(LINUX)
    target_link_libraries(${PLUGIN_NAME} PRIVATE
      GL X11 Xext Xinerama asound dl freetype pthread rt)
    set_property(TARGET ${PLUGIN_NAME} APPEND_STRING PROPERTY LINK_FLAGS
      "-fvisibility=hidden -fPIC -rdynamic -Wl,-rpath,'$$ORIGIN/../shared'")
    target_compile_options(${PLUGIN_NAME} PRIVATE -rdynamic)
    install(TARGETS ${PLUGIN_NAME} LIBRARY DESTINATION ${GUI_BIN_DIR}/plugins)
  elseif(APPLE)
    set_target_properties(${PLUGIN_NAME} PROPERTIES
      BUNDLE TRUE
      MACOSX_BUNDLE_BUNDLE_VERSION ${PLUGIN_VERSION}
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PLUGIN_VERSION})
    set_property(TARGET ${PLUGIN_NAME} APPEND_STRING PROPERTY LINK_FLAGS
      "-undefined dynamic_lookup -rpath @loader_path/../../../../shared")
    install(TARGETS ${PLUGIN_NAME}
            DESTINATION $ENV{HOME}/Library/Application\ Support/open-ephys/plugins-api${OE_GUI_API_VERSION})
  endif()

  source_group(TREE ${PROJECT_SOURCE_DIR}/Source PREFIX "Source" FILES ${ARGN})
endfunction()

# ---------------------------------------------------------------------------
add_subdirectory(Source/TriggerCore)
add_subdirectory(Source/Spectral)
add_subdirectory(Source/Power)
add_subdirectory(Source/Coherence)

# --- clang-format ----------------------------------------------------------
if(CLANG_FORMAT_EXE)
  file(GLOB_RECURSE ALL_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h)
  add_custom_target(format
    COMMAND ${CLANG_FORMAT_EXE} -i -style=file ${ALL_SOURCE_FILES}
    COMMENT "Running clang-format on source files" VERBATIM)
  add_custom_target(format-check
    COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror -style=file ${ALL_SOURCE_FILES}
    COMMENT "Checking code formatting with clang-format" VERBATIM)
endif()

# --- Tests -----------------------------------------------------------------
# Pulls the GUI in as a subproject to get gui_testable_source + test_helpers.
# Run this repo's tests with:  ctest -C Release
if(BUILD_TESTS)
  enable_testing()
  set(BUILD_TESTS ON CACHE BOOL "Enable testing" FORCE)
  set(OE_DONT_CHECK_BUILD_PATH TRUE CACHE BOOL "Disable build path check" FORCE)
  add_subdirectory(${GUI_BASE_DIR} main_gui_build EXCLUDE_FROM_ALL)
  add_subdirectory(Tests)
endif()
