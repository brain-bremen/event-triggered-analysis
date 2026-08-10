# TriggeredAvg Plugin Tests
# Uses the main GUI project's testable infrastructure (same approach as TestWithMainProject)

# Ensure this is being called from a parent CMakeLists.txt that provides necessary variables
if(NOT DEFINED GUI_BASE_DIR)
    message(FATAL_ERROR "GUI_BASE_DIR must be defined before including this CMakeLists.txt")
endif()

# Get plugin sources (assumes Source/CMakeLists.txt has been included)
# TRIGGERED_AVG_SOURCES and TRIGGERED_AVG_HEADERS should be available
if(NOT DEFINED TRIGGERED_AVG_SOURCES)
    message(FATAL_ERROR "TRIGGERED_AVG_SOURCES must be defined. Ensure Source/CMakeLists.txt is included first.")
endif()

# Test source files
set(TRIGGERED_AVG_TEST_SOURCES
    test_MultiChannelRingBuffer.cpp
    test_SingleTrialBuffer.cpp
    test_SingleTrialBuffer_RawPointers.cpp
    test_DataStore.cpp
    test_DataCollector.cpp
    test_PendingCapture.cpp
)

# Enable testing
enable_testing()

# Include Google Test module (assumes main project has already set it up)
include(GoogleTest)

# Create test executable
add_executable(${PLUGIN_NAME}_tests)

# Add sources to the test executable
target_sources(${PLUGIN_NAME}_tests PRIVATE
    # Plugin source files that need to be tested
    ${TRIGGERED_AVG_SOURCES}
    ${TRIGGERED_AVG_HEADERS}

    # Test files
    ${TRIGGERED_AVG_TEST_SOURCES}
)

# Link against the main project's testable infrastructure
target_link_libraries(${PLUGIN_NAME}_tests
    PRIVATE
        gui_testable_source    # From main project - provides JUCE + Open Ephys types
        test_helpers          # From main project - provides testing utilities
        gtest_main           # Google Test main function
)

# Set up include directories
target_include_directories(${PLUGIN_NAME}_tests PRIVATE
    # Plugin source directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source

    # Main project directories
    ${GUI_BASE_DIR}/JuceLibraryCode
    ${GUI_BASE_DIR}/JuceLibraryCode/modules
    ${GUI_BASE_DIR}/Plugins/Headers
    ${GUI_BASE_DIR}/Source

    # Test helpers
    ${GUI_BASE_DIR}/Tests/TestHelpers/include
)

# Set compile definitions - same as main project plugins
target_compile_definitions(${PLUGIN_NAME}_tests PRIVATE
    OEPLUGIN
    BUILD_TESTS
    "$<$<PLATFORM_ID:Windows>:JUCE_API=__declspec(dllimport)>"
    $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS>
    $<$<PLATFORM_ID:Linux>:JUCE_DISABLE_NATIVE_FILECHOOSERS=1>
    $<$<CONFIG:Debug>:DEBUG=1>
    $<$<CONFIG:Debug>:_DEBUG=1>
    $<$<CONFIG:Release>:NDEBUG=1>

    # Additional definitions that might be needed for testing
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
)

# Set C++ standard
target_compile_features(${PLUGIN_NAME}_tests PRIVATE cxx_std_23)

# Platform-specific compiler options (match main project)
if(MSVC)
    target_compile_options(${PLUGIN_NAME}_tests PRIVATE /sdl- /W0 /bigobj)
elseif(UNIX AND NOT APPLE)
    target_compile_options(${PLUGIN_NAME}_tests PRIVATE -fPIC -rdynamic -O3)
endif()

# Set the output directory to match the main project's test structure
set_target_properties(${PLUGIN_NAME}_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/TestBin/${PLUGIN_NAME}
)

# Copy DLLs from common directory to our test directory after build
add_custom_command(TARGET ${PLUGIN_NAME}_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/TestBin/common
    ${CMAKE_BINARY_DIR}/TestBin/${PLUGIN_NAME}
)

# Ensure DLLs are built before our tests
add_dependencies(${PLUGIN_NAME}_tests gui_testable_source test_helpers)

# Add tests to CTest so they can be discovered and run
gtest_discover_tests(${PLUGIN_NAME}_tests
    XML_OUTPUT_DIR "${CMAKE_BINARY_DIR}/test-results"
    DISCOVERY_TIMEOUT 30
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/TestBin/${PLUGIN_NAME}
    PROPERTIES
        LABELS "unit"
)

# Set the test executable as a Visual Studio startup project for easier debugging
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PLUGIN_NAME}_tests)

# Set Visual Studio debugger working directory to where DLLs are located
set_target_properties(${PLUGIN_NAME}_tests PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/TestBin/${PLUGIN_NAME}
)

# Print some helpful information
message(STATUS "Triggered Avg Tests Configuration:")
message(STATUS "  GUI Base Directory: ${GUI_BASE_DIR}")
message(STATUS "  Test Output Directory: ${CMAKE_BINARY_DIR}/TestBin/${PLUGIN_NAME}")
message(STATUS "  Plugin Sources: ${TRIGGERED_AVG_SOURCES}")
