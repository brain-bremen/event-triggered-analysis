# Tests, one binary per core layer.
#
# Reuses the GUI's own testable infrastructure (gui_testable_source + test_helpers),
# which is why the top-level CMakeLists adds the GUI as a subproject when
# BUILD_TESTS is on. Run them all with:
#     ctest -C Release
#
# The split is not cosmetic. trigger_core_tests links trigger_core and *not*
# spectra_core, so it will fail to build the day something FFTW-dependent is put
# on the wrong side of the line — which is the only automatic check that the
# core split stays real.

if(NOT DEFINED GUI_BASE_DIR)
    message(FATAL_ERROR "GUI_BASE_DIR must be defined before including this CMakeLists.txt")
endif()

enable_testing()
include(GoogleTest)

# Everything about triggering and buffering. No FFTW, by construction.
set(TRIGGER_CORE_TEST_SOURCES
    TriggerCore/test_MultiChannelRingBuffer.cpp
    TriggerCore/test_WorkQueue.cpp
    TriggerCore/test_CaptureWorker.cpp
    TriggerCore/test_TriggerMessaging.cpp
    TriggerCore/test_BroadcastMessageLog.cpp
)

# The frequency-domain half.
set(SPECTRA_TEST_SOURCES
    Spectral/test_FastSize.cpp
    Spectral/test_Fftw.cpp
    Spectral/test_Dpss.cpp
    Spectral/test_MorletTransform.cpp
    Spectral/test_TaperedPeriodogram.cpp
    Spectral/test_Accumulators.cpp
    Spectral/test_SpectralPipeline.cpp
    Spectral/test_TrialSpectrumBuffer.cpp
    Spectral/test_Whitening.cpp
    Spectral/test_Baseline.cpp
    Spectral/test_PairRules.cpp
    Spectral/test_ParameterLayout.cpp
)

# ---------------------------------------------------------------------------
# Shared wiring for one test binary.
#
# `core` is the library under test. `needs_fftw` decides whether the FFTW
# runtime is copied alongside — passing it for a target that does not link
# spectra_core would defeat the point of the split.
# ---------------------------------------------------------------------------
function(oe_add_test_binary TEST_NAME core needs_fftw)
    set(TEST_SOURCES ${ARGN})

    add_executable(${TEST_NAME} ${TEST_SOURCES})

    oe_apply_common_settings(${TEST_NAME})

    target_link_libraries(${TEST_NAME}
        PRIVATE
            ${core}
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

    # $<CONFIG> is required under a multi-config generator: without it the binary
    # lands in a per-config subdirectory that the runtime copies below never
    # reach, and the executable cannot start. That failure used to present as
    # ctest reporting "No tests were found" while exiting 0.
    set(TEST_OUTPUT_DIR ${CMAKE_BINARY_DIR}/TestBin/${TEST_NAME}/$<CONFIG>)

    set_target_properties(${TEST_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${TEST_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/TestBin/${TEST_NAME}/Debug
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/TestBin/${TEST_NAME}/Release
        VS_DEBUGGER_WORKING_DIRECTORY ${TEST_OUTPUT_DIR}
    )

    # The GUI's test DLLs land in TestBin/common, which a multi-config generator
    # splits by configuration. Copy the matching one — copying the whole tree
    # only ever worked because it recreated a Release/ subdirectory that happened
    # to be where the executable already was.
    add_custom_command(TARGET ${TEST_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_BINARY_DIR}/TestBin/common/$<CONFIG>
            ${TEST_OUTPUT_DIR}
    )

    if(MSVC AND needs_fftw)
        add_custom_command(TARGET ${TEST_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PROJECT_SOURCE_DIR}/libs/windows/bin/${CMAKE_LIBRARY_ARCHITECTURE}/libfftw3-3.dll
                ${TEST_OUTPUT_DIR}/
        )
    endif()

    add_dependencies(${TEST_NAME} gui_testable_source test_helpers)

    gtest_discover_tests(${TEST_NAME}
        XML_OUTPUT_DIR "${CMAKE_BINARY_DIR}/test-results"
        DISCOVERY_TIMEOUT 60
        WORKING_DIRECTORY ${TEST_OUTPUT_DIR}
        PROPERTIES LABELS "unit"
    )
endfunction()

oe_add_test_binary(trigger_core_tests trigger_core OFF ${TRIGGER_CORE_TEST_SOURCES})
oe_add_test_binary(spectra_tests      spectra_core ON  ${SPECTRA_TEST_SOURCES})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT spectra_tests)
