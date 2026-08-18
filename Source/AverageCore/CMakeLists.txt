# average_core: trial storage for the time-domain plugins, and the widgets that
# draw it — the single-trial ring, the running mean/SD accumulator, the
# per-source DataStore with its pending-commit path, and the channel x condition
# grid of trace panels.
#
# The display moved here with the storage for one reason: the receptive-field
# plugin needs to show the per-direction averages when a map looks wrong, and
# that view is exactly the one TriggeredAverage already draws. Reimplementing it
# would have meant two trace renderers that disagree about what the data says,
# which is the last thing a debugging view should do.
#
# The move cost nothing: GridDisplay and SinglePlotPanel each carried one
# #include of the averaging node, and neither used it.
#
# Layered on trigger_core exactly as spectra_core is, and free of FFTW for the
# same reason: this is what a plugin needs to *hold* trials, not what it computes
# from them. TriggeredAverage draws the traces; ReceptiveFieldMapper
# back-projects them into a map. Neither cares how the other displays them, and
# both want this storage unchanged.
#
# Built STATIC and linked into each plugin DLL.

set(AVERAGE_CORE_SOURCES
    DataCollector.cpp
    SingleTrialBuffer.cpp
    Ui/GridDisplay.cpp
    Ui/SinglePlotPanel.cpp
    Ui/TimeAxis.cpp
)

set(AVERAGE_CORE_HEADERS
    DataCollector.h
    SingleTrialBuffer.h
    Ui/DisplayMode.h
    Ui/GridDisplay.h
    Ui/SinglePlotPanel.h
    Ui/TimeAxis.h
    Ui/PerformanceTimer.h
)

add_library(average_core STATIC ${AVERAGE_CORE_SOURCES} ${AVERAGE_CORE_HEADERS})

oe_apply_common_settings(average_core)

# PUBLIC: a plugin linking average_core gets trigger_core's headers and symbols
# too, which is what lets oe_add_plugin() take a single core argument.
target_link_libraries(average_core PUBLIC trigger_core)

# Both this directory (for "DataCollector.h") and its parent (for
# "AverageCore/DataCollector.h" from a plugin or a test).
target_include_directories(average_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)

set_target_properties(average_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "AverageCore"
             FILES ${AVERAGE_CORE_SOURCES} ${AVERAGE_CORE_HEADERS})
