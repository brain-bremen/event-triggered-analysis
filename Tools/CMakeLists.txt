# rf_demo: renders a back-projection map from simulated data, with no GUI.
#
# The Phase 2 deliverable, and the reason the algorithm could be finished and
# checked before any Open Ephys code existed. Links rf_math and nothing else,
# so it builds on a checkout with no plugin-GUI present.

add_executable(rf_demo rf_demo.cpp PngWriter.h)

target_compile_features(rf_demo PRIVATE cxx_std_23)
target_link_libraries(rf_demo PRIVATE rf_math)
target_include_directories(rf_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(rf_demo PRIVATE /W4 /external:W0 /external:anglebrackets)
else()
    target_compile_options(rf_demo PRIVATE -Wall -Wextra)
endif()

set_target_properties(rf_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Tools
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Tools
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Tools)
