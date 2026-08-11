/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower,
    TriggeredCoherence and TriggeredAverage.
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
#pragma once

namespace EventTriggered
{

/** Implemented by an editor that shows how many trigger sources are
 *  configured, as a badge on its TRIGGERS button.
 *
 *  Driven from TriggeredCaptureNode::handleAsyncUpdate() — the same place
 *  refreshDisplay() is called from — rather than a separate listener
 *  registration, so the count can never fall out of sync with the trigger
 *  table on its own.
 */
class TriggerCountDisplay
{
public:
    virtual ~TriggerCountDisplay() = default;

    virtual void setTriggerCount (int count) = 0;
};

} // namespace EventTriggered
