# spectra_core: numerical + buffering core shared by both plugins.
#
# Built STATIC and linked into each plugin DLL. That costs a duplicated copy of
# the code per plugin (a few hundred KB) but avoids a third shared library with
# its own export macros and install step. FFTW stays a genuine shared dependency
# and is propagated to consumers via PUBLIC linkage.

set(SPECTRA_CORE_SOURCES
    Fftw.cpp
    FastSize.cpp
    MultiChannelRingBuffer.cpp
    TriggerSource.cpp
    TrialSpectrumBuffer.cpp
    SpectralWorker.cpp
    WorkQueue.cpp
    FrequencyGrid.cpp
    Dpss.cpp
    MorletTransform.cpp
    TaperedPeriodogram.cpp
    Accumulators.cpp
    SpectralEngine.cpp
    TriggerMessaging.cpp
    Whitening.cpp
    Baseline.cpp
    Ui/ColorMap.cpp
    Ui/SpectrumPanel.cpp
    Ui/PanelGrid.cpp
    Ui/TriggerSourceConfigWindow.cpp
    Ui/TriggerMonitorWindow.cpp
    TriggeredSpectraNode.cpp
)

set(SPECTRA_CORE_HEADERS
    Types.h
    Fftw.h
    FastSize.h
    MultiChannelRingBuffer.h
    TriggerSource.h
    TrialSpectrumBuffer.h
    SpectralWorker.h
    WorkQueue.h
    FrequencyGrid.h
    Tapers.h
    Dpss.h
    SpectralTransform.h
    MorletTransform.h
    TaperedPeriodogram.h
    Accumulators.h
    SpectralEngine.h
    TriggerMessaging.h
    Whitening.h
    Baseline.h
    Ui/ColorMap.h
    Ui/SpectrumPanel.h
    Ui/PanelGrid.h
    Ui/TriggerSourceConfigWindow.h
    Ui/TriggerMonitorWindow.h
    ParameterNames.h
    TriggeredSpectraNode.h
)

add_library(spectra_core STATIC ${SPECTRA_CORE_SOURCES} ${SPECTRA_CORE_HEADERS})

spectra_apply_common_settings(spectra_core)

target_include_directories(spectra_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_include_directories(spectra_core PUBLIC ${FFTW_INCLUDE_DIRS})
target_link_libraries(spectra_core PUBLIC ${FFTW_LIBRARY})

# The core references JUCE / Open Ephys symbols but does not link open-ephys.lib
# itself; those resolve at the final plugin link. Static libraries are allowed to
# carry unresolved externals, so this is fine on every supported toolchain.
set_target_properties(spectra_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Core"
             FILES ${SPECTRA_CORE_SOURCES} ${SPECTRA_CORE_HEADERS})
