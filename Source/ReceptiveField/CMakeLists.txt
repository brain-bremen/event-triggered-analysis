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
    ${CMAKE_CURRENT_SOURCE_DIR}/RfDemo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RfDemo.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfDisplayMode.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfCanvas.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfCanvas.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfMapPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfMapPanel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfAnalysisSettingsWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfAnalysisSettingsWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/StimulusConfigWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/StimulusConfigWindow.h
)

target_link_libraries(ReceptiveFieldMapper PRIVATE rf_math)
