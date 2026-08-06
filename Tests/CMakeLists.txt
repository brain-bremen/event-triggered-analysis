# Tests for the triggered-spectra core.
#
# Reuses the GUI's own testable infrastructure (gui_testable_source + test_helpers),
# which is why the top-level CMakeLists adds the GUI as a subproject when
# BUILD_TESTS is on. Run just these with:
#     ctest -R "TriggeredSpectra_tests" -C Release

if(NOT DEFINED GUI_BASE_DIR)
    message(FATAL_ERROR "GUI_BASE_DIR must be defined before including this CMakeLists.txt")
endif()

set(TEST_NAME TriggeredSpectra_tests)

set(SPECTRA_TEST_SOURCES
    test_FastSize.cpp
    test_MultiChannelRingBuffer.cpp
    test_TrialSpectrumBuffer.cpp
    test_Fftw.cpp
    test_Dpss.cpp
    test_MorletTransform.cpp
    test_TaperedPeriodogram.cpp
    test_Accumulators.cpp
    test_SpectralPipeline.cpp
    test_TriggerMessaging.cpp
    test_Whitening.cpp
)

enable_testing()
include(GoogleTest)

add_executable(${TEST_NAME} ${SPECTRA_TEST_SOURCES})

spectra_apply_common_settings(${TEST_NAME})

target_link_libraries(${TEST_NAME}
    PRIVATE
        spectra_core
        gui_testable_source   # JUCE + Open Ephys types
        test_helpers
        gtest_main
)

target_compile_definitions(${TEST_NAME} PRIVATE
    BUILD_TESTS
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
)

target_include_directories(${TEST_NAME} PRIVATE
    ${GUI_BASE_DIR}/Source
    ${GUI_BASE_DIR}/Tests/TestHelpers/include
)

if(MSVC)
    target_compile_options(${TEST_NAME} PRIVATE /bigobj)
endif()

set_target_properties(${TEST_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/TestBin/TriggeredSpectra
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/TestBin/TriggeredSpectra
)

# The GUI's test DLLs land in TestBin/common; the test binary needs them alongside.
add_custom_command(TARGET ${TEST_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/TestBin/common
        ${CMAKE_BINARY_DIR}/TestBin/TriggeredSpectra
)

# FFTW is a runtime dependency of spectra_core, so the test binary needs it too.
if(MSVC)
    add_custom_command(TARGET ${TEST_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/libs/windows/bin/${CMAKE_LIBRARY_ARCHITECTURE}/libfftw3-3.dll
            ${CMAKE_BINARY_DIR}/TestBin/TriggeredSpectra/
    )
endif()

add_dependencies(${TEST_NAME} gui_testable_source test_helpers)

gtest_discover_tests(${TEST_NAME}
    XML_OUTPUT_DIR "${CMAKE_BINARY_DIR}/test-results"
    DISCOVERY_TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/TestBin/TriggeredSpectra
    PROPERTIES LABELS "unit"
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${TEST_NAME})
