# trigger_core: everything an event-triggered plugin needs before it knows what
# it is estimating — the ring buffer, the trigger sources, the work queue, the
# capture worker, the broadcast-message path, and the two windows used to set
# triggers up and watch them fire.
#
# Deliberately free of FFTW and of any DSP. That is not tidiness: the time-domain
# averaging plugin links this and nothing else, and a numerical dependency here
# would be inherited by a plugin with no use for it. If this target ever starts
# needing FFTW, something has been put on the wrong side of the line.
#
# Built STATIC and linked into each plugin DLL, like spectra_core.

set(TRIGGER_CORE_SOURCES
    MultiChannelRingBuffer.cpp
    WorkQueue.cpp
    CaptureWorker.cpp
    TriggerSource.cpp
    TriggerMessaging.cpp
    BroadcastMessageLog.cpp
    TriggeredCaptureNode.cpp
    Ui/ParameterControl.cpp
    Ui/TriggerSourceConfigWindow.cpp
    Ui/TriggerMonitorWindow.cpp
)

set(TRIGGER_CORE_HEADERS
    Types.h
    ParameterNames.h
    MultiChannelRingBuffer.h
    WorkQueue.h
    CaptureWorker.h
    TriggerSource.h
    TriggerMessaging.h
    BroadcastMessageLog.h
    TriggeredCaptureNode.h
    Ui/ParameterControl.h
    Ui/TriggerSourceConfigWindow.h
    Ui/TriggerMonitorWindow.h
)

add_library(trigger_core STATIC ${TRIGGER_CORE_SOURCES} ${TRIGGER_CORE_HEADERS})

spectra_apply_common_settings(trigger_core)

# Both this directory (for "TriggerSource.h") and its parent (for
# "TriggerCore/TriggerSource.h" from a plugin).
target_include_directories(trigger_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)

# References JUCE / Open Ephys symbols but does not link open-ephys.lib itself;
# those resolve at the final plugin link. Static libraries are allowed to carry
# unresolved externals, so this is fine on every supported toolchain.
set_target_properties(trigger_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "TriggerCore"
             FILES ${TRIGGER_CORE_SOURCES} ${TRIGGER_CORE_HEADERS})
