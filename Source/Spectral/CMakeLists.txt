# spectra_core: the frequency-domain half — FFTW, DPSS tapers, Morlet wavelets,
# the accumulators, and the spectral display widgets.
#
# Layered on trigger_core rather than beside it: everything here consumes trial
# windows that trigger_core produced, and nothing here is needed to produce them.
# FFTW is a genuine shared dependency and is propagated to consumers via PUBLIC
# linkage.

set(SPECTRA_CORE_SOURCES
    Fftw.cpp
    FastSize.cpp
    FrequencyGrid.cpp
    Dpss.cpp
    MorletTransform.cpp
    TaperedPeriodogram.cpp
    Accumulators.cpp
    SpectralEngine.cpp
    TrialSpectrumBuffer.cpp
    Whitening.cpp
    Baseline.cpp
    PairRules.cpp
    TriggeredSpectraNode.cpp
    Ui/ColorMap.cpp
    Ui/SpectrumPanel.cpp
    Ui/PanelGrid.cpp
    Ui/AnalysisSettingsWindow.cpp
)

set(SPECTRA_CORE_HEADERS
    SpectralTypes.h
    SpectralParameterNames.h
    Fftw.h
    FastSize.h
    FrequencyGrid.h
    Tapers.h
    Dpss.h
    SpectralTransform.h
    MorletTransform.h
    TaperedPeriodogram.h
    Accumulators.h
    SpectralEngine.h
    TrialSpectrumBuffer.h
    Whitening.h
    Baseline.h
    PairRules.h
    TriggeredSpectraNode.h
    Ui/ColorMap.h
    Ui/SpectrumPanel.h
    Ui/PanelGrid.h
    Ui/AnalysisSettingsWindow.h
    Ui/ParameterLayout.h
    Ui/EditorLayout.h
)

add_library(spectra_core STATIC ${SPECTRA_CORE_SOURCES} ${SPECTRA_CORE_HEADERS})

spectra_apply_common_settings(spectra_core)

target_include_directories(spectra_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_include_directories(spectra_core PUBLIC ${FFTW_INCLUDE_DIRS})
target_link_libraries(spectra_core PUBLIC trigger_core ${FFTW_LIBRARY})

set_target_properties(spectra_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Spectral"
             FILES ${SPECTRA_CORE_SOURCES} ${SPECTRA_CORE_HEADERS})
