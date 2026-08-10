# spectra_core: the frequency-domain half — FFTW, DPSS tapers, Morlet wavelets,
# the accumulators, and the spectral display widgets.
#
# Layered on trigger_core rather than beside it: everything here consumes trial
# windows that trigger_core produced, and nothing here is needed to produce them.
# FFTW is a genuine shared dependency and is propagated to consumers via PUBLIC
# linkage.

# --- Vendored FFTW3 --------------------------------------------------------
# Binaries copied from the OpenEphysFFTW common library. Double precision only;
# there is no fftwf in this distribution.
#
# Discovered here rather than at the top level so that a build of trigger_core
# alone never looks for it. PROJECT_SOURCE_DIR, not CMAKE_CURRENT_SOURCE_DIR:
# libs/ lives at the repo root, two directories up.
if(MSVC)
  list(APPEND CMAKE_PREFIX_PATH ${PROJECT_SOURCE_DIR}/libs/windows)
elseif(LINUX)
  list(APPEND CMAKE_PREFIX_PATH ${PROJECT_SOURCE_DIR}/libs/linux)
elseif(APPLE)
  list(APPEND CMAKE_PREFIX_PATH ${PROJECT_SOURCE_DIR}/libs/macos)
endif()

find_library(FFTW_LIBRARY NAMES libfftw3-3 fftw3 fftw3-3)
find_path(FFTW_INCLUDE_DIRS fftw3.h)

if (NOT FFTW_LIBRARY)
  message(FATAL_ERROR "FFTW library not found (looked under ${PROJECT_SOURCE_DIR}/libs)")
endif()
if (NOT FFTW_INCLUDE_DIRS)
  message(FATAL_ERROR "fftw3.h not found (looked under ${PROJECT_SOURCE_DIR}/libs)")
endif()
message(STATUS "FFTW library: ${FFTW_LIBRARY}")
message(STATUS "FFTW headers: ${FFTW_INCLUDE_DIRS}")

# Ship the FFTW runtime alongside the GUI. Both spectral plugins load the *same*
# libfftw3-3.dll, which is why Fftw.cpp guards the planner with a process-wide
# lock (this build does not export fftw_make_planner_thread_safe).
if (MSVC)
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/libs/windows/bin/${CMAKE_LIBRARY_ARCHITECTURE}/
          DESTINATION ${GUI_BIN_DIR}/shared
          CONFIGURATIONS ${CMAKE_CONFIGURATION_TYPES})
elseif(LINUX)
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/libs/linux/bin/
          DESTINATION ${GUI_BIN_DIR}/shared)
elseif(APPLE)
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/libs/macos/bin/
          DESTINATION $ENV{HOME}/Library/Application\ Support/open-ephys/shared-api${OE_GUI_API_VERSION})
endif()

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
