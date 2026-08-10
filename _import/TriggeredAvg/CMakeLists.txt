cmake_minimum_required(VERSION 3.23.0)

set(PLUGIN_NAME TriggeredAvg)

# Plugin version information
set(PLUGIN_VERSION_MAJOR 0)
set(PLUGIN_VERSION_MINOR 2)
set(PLUGIN_VERSION_PATCH 1)
set(PLUGIN_VERSION "${PLUGIN_VERSION_MAJOR}.${PLUGIN_VERSION_MINOR}.${PLUGIN_VERSION_PATCH}")

# Open Ephys GUI API version this plugin is built for
set(OE_GUI_API_VERSION 10)
# Optional: Set the GUI version if known (e.g., 0.6.5)
# This can be auto-detected or manually set
set(OE_GUI_VERSION "1.0.x" CACHE STRING "Open Ephys GUI version")

project(OE_PLUGIN_${PLUGIN_NAME} VERSION ${PLUGIN_VERSION})

# Option to build tests
option(BUILD_TESTS "Build the test suite" ON)

if (NOT DEFINED GUI_BASE_DIR)
  if (DEFINED ENV{GUI_BASE_DIR})
    set(GUI_BASE_DIR $ENV{GUI_BASE_DIR})
  else()
    set(GUI_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../plugin-GUI)
  endif()
endif()

# ensure GUI folder exists
if (NOT EXISTS ${GUI_BASE_DIR})
  message(FATAL_ERROR "GUI_BASE_DIR does not exist: ${GUI_BASE_DIR}")
endif()


get_filename_component(PROJECT_FOLDER ${CMAKE_CURRENT_SOURCE_DIR} ABSOLUTE)

set(CMAKE_SHARED_LIBRARY_PREFIX "")
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  set(LINUX 1)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
  endif()
endif()

set(TRIGGERED_AVG_SOURCE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Source)
set(GUI_COMMONLIB_DIR ${GUI_BASE_DIR}/installed_libs)
set(CONFIGURATION_FOLDER $<$<CONFIG:Debug>:Debug>$<$<NOT:$<CONFIG:Debug>>:Release>)
list(APPEND CMAKE_PREFIX_PATH ${GUI_COMMONLIB_DIR} ${GUI_COMMONLIB_DIR}/${CONFIGURATION_FOLDER})
set(GUI_BIN_DIR ${GUI_BASE_DIR}/Build/${CONFIGURATION_FOLDER})
message(DEBUG "Using GUI_BASE_DIR: " ${GUI_BASE_DIR})
message(DEBUG "GUI_BIN_DIR will be: " ${GUI_BASE_DIR}/Build/[Debug|Release])

# Setup clang-format target
find_program(CLANG_FORMAT_EXE NAMES "clang-format")
if(CLANG_FORMAT_EXE)
  message(STATUS "clang-format found: ${CLANG_FORMAT_EXE}")
endif()

add_subdirectory(${TRIGGERED_AVG_SOURCE_PATH}) # sets TRIGGERED_AVG_HEADERS and TRIGGERED_AVG_SOURCES
MESSAGE(DEBUG "TRIGGERED_AVG_HEADERS: ${TRIGGERED_AVG_HEADERS}")
MESSAGE(DEBUG "TRIGGERED_AVG_SOURCES: ${TRIGGERED_AVG_SOURCES}")

# Get GUI_VERSION from the plugin-GUI CMakeLists.txt
if(EXISTS "${GUI_BASE_DIR}/CMakeLists.txt")
  # Create a temporary scope to read GUI_VERSION without building anything
  file(READ "${GUI_BASE_DIR}/CMakeLists.txt" GUI_CMAKE_CONTENT)
  string(REGEX MATCH "set\\(GUI_VERSION ([^)]+)\\)" _ "${GUI_CMAKE_CONTENT}")
  if(CMAKE_MATCH_1)
    string(STRIP "${CMAKE_MATCH_1}" GUI_VERSION_EXTRACTED)
    string(REPLACE "\"" "" GUI_VERSION_EXTRACTED "${GUI_VERSION_EXTRACTED}")
    set(OE_GUI_VERSION "${GUI_VERSION_EXTRACTED}")
    message(STATUS "Detected Open Ephys GUI version: ${OE_GUI_VERSION}")
  endif()
endif()

# Generate version header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginVersion.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/PluginVersion.h
    @ONLY
)

# Add Windows resource file for DLL version info
if(WIN32)
  configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/Resources/Version.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/Version.rc
        @ONLY
    )
  set(RESOURCE_FILES ${CMAKE_CURRENT_BINARY_DIR}/Version.rc)
endif()

if (APPLE)
  add_library(${PLUGIN_NAME} MODULE ${TRIGGERED_AVG_SOURCES} ${RESOURCE_FILES})
else()
  add_library(${PLUGIN_NAME} SHARED ${TRIGGERED_AVG_SOURCES} ${RESOURCE_FILES})
endif()

target_sources(${PLUGIN_NAME}
 PRIVATE
  FILE_SET HEADERS
  BASE_DIRS ${TRIGGERED_AVG_SOURCE_PATH}
  FILES
   ${TRIGGERED_AVG_HEADERS}
 PRIVATE
    ${TRIGGERED_AVG_SOURCES}
)

# Organize files into folders in Visual Studio Solution Explorer
source_group(TREE ${TRIGGERED_AVG_SOURCE_PATH}
             PREFIX "Source Files"
             FILES ${TRIGGERED_AVG_SOURCES})
source_group(TREE ${TRIGGERED_AVG_SOURCE_PATH}
             PREFIX "Header Files"
             FILES ${TRIGGERED_AVG_HEADERS})

target_compile_definitions(${PLUGIN_NAME}
    PRIVATE
        OEPLUGIN
        "$<$<PLATFORM_ID:Windows>:JUCE_API=__declspec(dllimport)>"
        $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>
        $<$<PLATFORM_ID:Linux>:JUCE_DISABLE_NATIVE_FILECHOOSERS=1>
        $<$<CONFIG:Debug>:DEBUG=1>
        $<$<CONFIG:Debug>:_DEBUG=1>
        $<$<CONFIG:Release>:NDEBUG=1>
)
target_compile_features(${PLUGIN_NAME} PUBLIC cxx_auto_type cxx_generalized_initializers cxx_std_23)
target_include_directories(${PLUGIN_NAME}
  PUBLIC
  ${GUI_BASE_DIR}/JuceLibraryCode
  ${GUI_BASE_DIR}/JuceLibraryCode/modules
  ${GUI_BASE_DIR}/Plugins/Headers
  ${GUI_COMMONLIB_DIR}/include
  ${CMAKE_CURRENT_BINARY_DIR}  # For generated PluginVersion.h
)

# Set library version properties
set_target_properties(${PLUGIN_NAME} PROPERTIES
    VERSION ${PLUGIN_VERSION}
    SOVERSION ${PLUGIN_VERSION_MAJOR}
)


if(MSVC)
  target_link_libraries(${PLUGIN_NAME}
  ${GUI_BIN_DIR}/open-ephys.lib
 )

  target_compile_options(${PLUGIN_NAME} PRIVATE /sdl /W3 /MP4 /external:W0
                           /external:anglebrackets)

  install(TARGETS ${PLUGIN_NAME} RUNTIME DESTINATION ${GUI_BIN_DIR}/plugins  CONFIGURATIONS ${CMAKE_CONFIGURATION_TYPES})

  set(CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../libs)
elseif(LINUX)
  target_link_libraries(${PLUGIN_NAME}
  GL X11 Xext Xinerama asound dl freetype pthread rt
 )
  set_property(TARGET ${PLUGIN_NAME} APPEND_STRING PROPERTY LINK_FLAGS
  "-fvisibility=hidden -fPIC -rdynamic -Wl,-rpath,'$$ORIGIN/../shared'")
  target_compile_options(${PLUGIN_NAME} PRIVATE -fPIC -rdynamic)
  target_compile_options(${PLUGIN_NAME} PRIVATE -O3) #enable optimization for linux debug

  install(TARGETS ${PLUGIN_NAME} LIBRARY DESTINATION ${GUI_BIN_DIR}/plugins)
elseif(APPLE)
  set_target_properties(${PLUGIN_NAME} PROPERTIES
        BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_VERSION ${PLUGIN_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PLUGIN_VERSION}
    )
  set_property(TARGET ${PLUGIN_NAME} APPEND_STRING PROPERTY LINK_FLAGS
 "-undefined dynamic_lookup -rpath @loader_path/../../../../shared")

  install(TARGETS ${PLUGIN_NAME} DESTINATION $ENV{HOME}/Library/Application\ Support/open-ephys/plugins-api10)
  set(CMAKE_PREFIX_PATH /opt/local)
endif()

# Add clang-format target if clang-format is available
if(CLANG_FORMAT_EXE)
  # Collect all source files for formatting
  file(GLOB_RECURSE ALL_SOURCE_FILES
        ${TRIGGERED_AVG_SOURCE_PATH}/*.cpp
        ${TRIGGERED_AVG_SOURCE_PATH}/*.h
    )

  # Add format target
  add_custom_target(
        format
        COMMAND ${CLANG_FORMAT_EXE} -i -style=file ${ALL_SOURCE_FILES}
        COMMENT "Running clang-format on source files"
        VERBATIM
    )

  # Add format-check target (doesn't modify files, just checks)
  add_custom_target(
        format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror -style=file ${ALL_SOURCE_FILES}
        COMMENT "Checking code formatting with clang-format"
        VERBATIM
    )

  message(STATUS "Added 'format' and 'format-check' targets")
endif()

# Add tests subdirectory if BUILD_TESTS is enabled
if(BUILD_TESTS)
  # Enable testing at the top level so CTest can discover tests
  enable_testing()

  # Build the main GUI project with tests enabled to get gui_testable_source and test_helpers
  # NOTE: This will also register GUI tests in CTest. To run only TriggeredAvg tests:
  #   ctest -R "TriggeredAvg_tests" -C Release
  # Or from the Tests subdirectory:
  #   cd Build/Tests && ctest -C Release
  set(BUILD_TESTS ON CACHE BOOL "Enable testing" FORCE)
  set(OE_DONT_CHECK_BUILD_PATH TRUE CACHE BOOL "Disable build path check" FORCE)
  add_subdirectory(${GUI_BASE_DIR} main_gui_build EXCLUDE_FROM_ALL)

  # Now add our plugin tests
  add_subdirectory(Tests)
endif()
