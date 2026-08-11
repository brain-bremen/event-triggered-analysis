# TriggeredAverage: time-domain average of continuous data around each trigger.
#
# Links trigger_core and *not* spectra_core. That is the whole reason the core
# was split: this plugin needs trial windows and nothing frequency-domain, so it
# must not inherit FFTW.

oe_add_plugin(TriggeredAverage trigger_core
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenEphysLib.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TriggeredAvgNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TriggeredAvgNode.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DataCollector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DataCollector.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SingleTrialBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SingleTrialBuffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/DisplayMode.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/PerformanceTimer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/GridDisplay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/GridDisplay.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/SinglePlotPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/SinglePlotPanel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/TimeAxis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/TimeAxis.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/TriggeredAvgCanvas.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/TriggeredAvgCanvas.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/TriggeredAvgEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/TriggeredAvgEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/AvgAnalysisSettingsWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/AvgAnalysisSettingsWindow.h
)
