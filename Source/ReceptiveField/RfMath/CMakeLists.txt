# rf_math: the receptive-field mapping algorithm, and nothing else.
#
# No JUCE, no Open Ephys, no FFTW. That is not tidiness either: the whole of the
# back-projection method is arithmetic over spans of floats, so keeping it free
# of the GUI means essentially all of it is reachable from plain unit tests that
# build in seconds. rf_math_tests links this and gtest_main *only*, which is what
# keeps the promise honest — the day something here includes JuceHeader.h, that
# binary stops linking.
#
# It also means this library can be compiled and tested on a checkout with no
# Open Ephys GUI present at all.

set(RF_MATH_SOURCES
    ResponseProfile.cpp
    BackProjection.cpp
    RfMetrics.cpp
    RfSimulator.cpp
    StimulusGeometry.cpp
)

set(RF_MATH_HEADERS
    AngleConvention.h
    MapGeometry.h
    StimulusGeometry.h
    ResponseProfile.h
    BackProjection.h
    RfMetrics.h
    RfSimulator.h
)

add_library(rf_math STATIC ${RF_MATH_SOURCES} ${RF_MATH_HEADERS})

# Deliberately *not* oe_apply_common_settings(): that pulls in the JUCE and Open
# Ephys include directories, and this target must not have them.
target_compile_features(rf_math PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(rf_math PRIVATE /W4 /external:W0 /external:anglebrackets)
else()
    target_compile_options(rf_math PRIVATE -fPIC -Wall -Wextra)
    target_compile_options(rf_math PRIVATE $<$<NOT:$<CONFIG:Debug>>:-O3>)
endif()

# Both this directory (for "BackProjection.h") and its parent's parent (for
# "RfMath/BackProjection.h" from the plugin or a test).
target_include_directories(rf_math PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)

set_target_properties(rf_math PROPERTIES POSITION_INDEPENDENT_CODE ON)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "RfMath"
             FILES ${RF_MATH_SOURCES} ${RF_MATH_HEADERS})
