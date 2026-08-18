# ReceptiveFieldBarMapper: receptive fields from bars swept in N directions.
#
# Named for the stimulus because the stimulus decides the analysis, not just the
# condition table. Sweeping bars give one-dimensional projections of the
# receptive field, which back-projection intersects; flashed dots give samples of
# a two-dimensional field, which is reverse correlation and shares almost no
# arithmetic with this. Wedges and rings are different again. So a dot mapper is
# not this plugin with a different stimulus dialog -- it is a different
# estimator, and calling this one ReceptiveFieldMapper would have claimed the
# shared name for whichever estimator happened to be written first.
#
# What a second mapper would actually reuse is narrower than it looks:
#
#   MapGeometry, Map2D, RfMetrics        the map itself, and peak / contour /
#                                        size measured on it. Estimator-agnostic.
#   RfMapPanel, RfMapGrid, RfComputeJob  drawing a Map2D, and recomputing one off
#                                        the message thread.
#
# Everything else in rf_math is about bars specifically -- BackProjection,
# ResponseProfile (time to space along an axis of motion), SweepGeometry,
# RfSimulator, RfPipeline -- as is SweepAngles, StimulusConfigWindow and
# BarMapperNode itself.
#
# Deliberately not split into two libraries yet. The boundary above is real, but
# there is no second consumer to hold it honest, and a layer with one user is a
# guess about the next one. The day a dot mapper starts, the first commit is that
# split, and it is a small one.
#
# Links average_core (which brings trigger_core with it) for the accumulators and
# the capture path, plus rf_math for the algorithm. No FFTW: like TriggeredAverage
# this plugin never leaves the time domain.

oe_add_plugin(ReceptiveFieldBarMapper average_core
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenEphysLib.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BarMapperNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BarMapperNode.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RfComputeJob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RfComputeJob.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SweepAngles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SweepAngles.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RfDemo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RfDemo.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfDisplayMode.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfCanvas.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfCanvas.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/BarMapperEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/BarMapperEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfMapPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfMapPanel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfAnalysisSettingsWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/RfAnalysisSettingsWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/StimulusConfigWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Ui/StimulusConfigWindow.h
)

target_link_libraries(ReceptiveFieldBarMapper PRIVATE rf_math)
