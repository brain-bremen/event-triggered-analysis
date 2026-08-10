/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2026 Joscha Schmiedt, Universität Bremen

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "TriggerMessaging.h"

namespace EventTriggered
{

TriggerMessageActions matchTriggerMessage (const TriggerSource& source,
                                           const juce::String& message)
{
    TriggerMessageActions actions;

    if (message.isEmpty())
        return actions;

    // An empty pattern means "disabled". Without this guard
    // containsIgnoreCase("") is true for every message, so an unconfigured source
    // would fire on all broadcast traffic.
    if (source.armPattern.isNotEmpty() && message.containsIgnoreCase (source.armPattern))
        actions.arm = true;

    if (source.cancelPattern.isNotEmpty() && message.containsIgnoreCase (source.cancelPattern))
        actions.cancel = true;

    if (source.commitPattern.isNotEmpty() && message.containsIgnoreCase (source.commitPattern))
        actions.commit = true;

    return actions;
}

TriggerStateChange applyTriggerMessage (TriggerSource& source,
                                        const TriggerMessageActions& actions)
{
    TriggerStateChange change;

    if (actions.cancel)
    {
        // A parked capture is discarded whatever the source type: cancelling a
        // trial is meaningful even for an always-live TTL source.
        change.discardPending = true;

        // Disarming, however, only applies to message-gated sources. Clearing the
        // flag on a plain TTL source would disable it for the rest of the session.
        if (isMessageGated (source))
            source.canTrigger = false;
    }
    else if (actions.commit)
    {
        change.commitPending = true;
    }

    if (actions.arm && isMessageGated (source))
        source.canTrigger = true;

    return change;
}

} // namespace EventTriggered
