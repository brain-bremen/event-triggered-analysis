# ReceptiveFieldMapper: back-projection RF mapping from per-direction averages.
#
# Links average_core (which brings trigger_core with it) for the accumulators and
# the capture path, plus rf_math for the algorithm. No FFTW: like TriggeredAverage
# this plugin never leaves the time domain.

oe_add_plugin(ReceptiveFieldMapper average_core
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenEphysLib.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ReceptiveFieldNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ReceptiveFieldNode.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RfComputeJob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RfComputeJob.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SweepAngles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SweepAngles.h
)

target_link_libraries(ReceptiveFieldMapper PRIVATE rf_math)
