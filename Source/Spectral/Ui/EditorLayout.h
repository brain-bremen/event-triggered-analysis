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
#pragma once

#include "../SpectralParameterNames.h"

#include "TriggerCore/Ui/EditorLayout.h"

#include <EditorHeaders.h>
#include <JuceHeader.h>

namespace EventTriggered
{

/** Editor layout for the two spectral plugins.
 *
 *  The buttons, the channel selector and the pre/post row are common to every
 *  triggered plugin and live in TriggerCore/Ui/EditorLayout.h; all this adds is
 *  the estimate-mode selector on the last row.
 */
inline void layoutEditorContents (GenericEditor& editor,
                                  juce::Component* triggersButton,
                                  juce::Component* analysisButton,
                                  juce::Component* monitorButton,
                                  juce::Component* pairsButton = nullptr)
{
    const int y = EditorLayout::layoutCommonContents (
        editor, { triggersButton, analysisButton, monitorButton, pairsButton });

    EditorLayout::layoutLastRow (editor, editor.getParameterEditor (ParameterNames::mode), y);
}

} // namespace EventTriggered
