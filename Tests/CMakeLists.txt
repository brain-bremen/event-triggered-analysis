# Tests, one binary per core layer.
#
# Reuses the GUI's own testable infrastructure (gui_testable_source + test_helpers),
# which is why the top-level CMakeLists adds the GUI as a subproject when
# BUILD_TESTS is on. Run them all with:
#     ctest --test-dir Build/Tests -C Release
#
# Run them from Build/Tests, not the build root: adding the GUI as a subproject
# also registers its own test targets, which are EXCLUDE_FROM_ALL and never built
# here, so from the root ctest finds them and fails on the missing executables.
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

# Where gui_testable_source and test_helpers put their runtime libraries.
#
# A multi-config generator (Visual Studio) appends the configuration to every
# output directory; a single-config one (Makefiles, Ninja) does not. The GUI sets
# that output directory without a $<CONFIG> component, so the two disagree, and
# hardcoding either layout breaks the other platform's build — copying from
# TestBin/common/$<CONFIG> failed on Linux with "Error copying directory",
# because there is no Release subdirectory there.
if(CMAKE_CONFIGURATION_TYPES)
    set(COMMON_TEST_RUNTIME_DIR ${CMAKE_BINARY_DIR}/TestBin/common/$<CONFIG>)
else()
    set(COMMON_TEST_RUNTIME_DIR ${CMAKE_BINARY_DIR}/TestBin/common)
endif()

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

# The trial storage shared by TriggeredAverage and ReceptiveFieldMapper. Links
# average_core rather than compiling its sources directly, which is what makes
# these tests exercise the same library the plugins link.
set(AVERAGE_TEST_SOURCES
    Average/test_DataStore.cpp
    Average/test_AverageAccumulation.cpp
    Average/test_PendingCapture.cpp
    Average/test_SingleTrialBuffer.cpp
    Average/test_SingleTrialBuffer_RawPointers.cpp
)

# The receptive-field algorithm. Wired up separately, below, because it links
# neither the GUI nor any core in this repository.
set(RF_MATH_TEST_SOURCES
    ReceptiveField/test_AngleConvention.cpp
    ReceptiveField/test_StimulusGeometry.cpp
    ReceptiveField/test_ResponseProfile.cpp
    ReceptiveField/test_BackProjection.cpp
    ReceptiveField/test_LatencyScan.cpp
    ReceptiveField/test_RfMetrics.cpp
    ReceptiveField/test_Pipeline.cpp
    ReceptiveField/test_RfPipeline.cpp
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

    # The GUI's test libraries have to sit beside the executable. See
    # COMMON_TEST_RUNTIME_DIR above for why the source path is not a constant.
    add_custom_command(TARGET ${TEST_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${COMMON_TEST_RUNTIME_DIR}
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
oe_add_test_binary(average_tests      average_core OFF ${AVERAGE_TEST_SOURCES})

# The receptive-field plugin's own glue: the angle table and the arm patterns it
# generates. Needs JUCE and trigger_core (it matches against real message
# strings), which is why it cannot live in rf_math_tests.
set(RF_NODE_TEST_SOURCES
    ReceptiveField/test_SweepAngles.cpp
    ReceptiveField/test_RfDemo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/ReceptiveField/SweepAngles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/ReceptiveField/RfDemo.cpp
)

oe_add_test_binary(rf_node_tests      average_core OFF ${RF_NODE_TEST_SOURCES})
target_link_libraries(rf_node_tests PRIVATE rf_math)
target_include_directories(rf_node_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source)

# rf_math_tests is deliberately *not* built with oe_add_test_binary(): it links
# rf_math and gtest_main and nothing else — no gui_testable_source, no
# test_helpers, no JUCE include directories. That is the automatic check that the
# algorithm stays free of the GUI, in the same way trigger_core_tests is the
# check that trigger_core stays free of FFTW. It is also why this binary builds
# in seconds while the others build the Open Ephys GUI first.
add_executable(rf_math_tests ${RF_MATH_TEST_SOURCES})

target_compile_features(rf_math_tests PRIVATE cxx_std_23)
target_link_libraries(rf_math_tests PRIVATE rf_math gtest_main)
target_include_directories(rf_math_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ReceptiveField)

if(MSVC)
    target_compile_options(rf_math_tests PRIVATE /W4 /bigobj /external:W0 /external:anglebrackets)
else()
    target_compile_options(rf_math_tests PRIVATE -Wall -Wextra)
endif()

set_target_properties(rf_math_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/TestBin/rf_math_tests/$<CONFIG>
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/TestBin/rf_math_tests/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/TestBin/rf_math_tests/Release)

gtest_discover_tests(rf_math_tests
    XML_OUTPUT_DIR "${CMAKE_BINARY_DIR}/test-results"
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT spectra_tests)
