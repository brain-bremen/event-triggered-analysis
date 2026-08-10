set(TRIGGERED_AVG_SOURCES_RELATIVE
    DataCollector.cpp
    MultiChannelRingBuffer.cpp
    OpenEphysLib.cpp
    SingleTrialBuffer.cpp
    TriggeredAvgActions.cpp
    TriggeredAvgNode.cpp
    TriggerSource.cpp
    Ui/GridDisplay.cpp
    Ui/PopupConfigurationWindow.cpp
    Ui/SinglePlotPanel.cpp
    Ui/TimeAxis.cpp
    Ui/TriggeredAvgCanvas.cpp
    Ui/TriggeredAvgEditor.cpp
)

set(TRIGGERED_AVG_HEADERS_RELATIVE
    DataCollector.h
    MultiChannelRingBuffer.h
    SingleTrialBuffer.h
    TriggeredAvgActions.h
    TriggeredAvgNode.h
    TriggerSource.h
    Ui/DisplayMode.h
    Ui/GridDisplay.h
    Ui/SinglePlotPanel.h
    Ui/TimeAxis.h
    Ui/TriggeredAvgCanvas.h
    Ui/TriggeredAvgEditor.h
)

# Convert to full paths
foreach(src ${TRIGGERED_AVG_SOURCES_RELATIVE})
    list(APPEND TRIGGERED_AVG_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${src})
endforeach()

foreach(hdr ${TRIGGERED_AVG_HEADERS_RELATIVE})
    list(APPEND TRIGGERED_AVG_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/${hdr})
endforeach()

# Set variables in parent scope
set(TRIGGERED_AVG_SOURCES ${TRIGGERED_AVG_SOURCES} PARENT_SCOPE)
set(TRIGGERED_AVG_HEADERS ${TRIGGERED_AVG_HEADERS} PARENT_SCOPE)
