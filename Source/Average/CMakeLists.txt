# TriggeredAverage: time-domain average of continuous data around each trigger.
#
# Links average_core (which brings trigger_core with it) and *not* spectra_core.
# That is the whole reason the cores were split: this plugin needs trial windows
# and nothing frequency-domain, so it must not inherit FFTW.
#
# The storage itself lives in average_core because ReceptiveFieldMapper wants the
# same accumulators; everything left here is this plugin's own display.

oe_add_plugin(TriggeredAverage average_core
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenEphysLib.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TriggeredAvgNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TriggeredAvgNode.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/TriggeredAvgCanvas.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/TriggeredAvgCanvas.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/TriggeredAvgEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/TriggeredAvgEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/AvgAnalysisSettingsWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/AvgAnalysisSettingsWindow.h
)
