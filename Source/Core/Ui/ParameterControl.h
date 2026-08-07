/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2025-2026 Joscha Schmiedt, Universität Bremen

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

#include <JuceHeader.h>
#include <ProcessorHeaders.h>
#include <functional>
#include <memory>

namespace TriggeredSpectra
{

/** One Open Ephys `Parameter`, with a control appropriate to its type.
 *
 *  The GUI's own `ParameterEditor` machinery expects to be owned by a
 *  `ParameterEditorOwner` and rebound by `Visualizer::update()`. That is more
 *  ceremony than these panels need, and the canvases already drive plain JUCE
 *  widgets for their display state, so this binds directly instead: read through
 *  the parameter, write through `setNextValue`.
 *
 *  It exists so the three places that show parameters — the analysis popup and
 *  both canvas option bars — share one implementation of the fiddly parts: type
 *  dispatch, clamping to the parameter's own range, and writing the accepted
 *  value back so the control can never disagree with the parameter.
 *
 *  Categorical parameters get a combo box; float and int get an editable label.
 *  Anything else is left inert rather than guessed at.
 */
class ParameterControl : public juce::Component
{
public:
    /** @param nameWidth     width of the parameter's display name; 0 omits it
        @param controlWidth  width of the combo box or editable field
        @param unitWidth     width of the trailing unit; 0 omits it */
    ParameterControl (Parameter* parameter, int nameWidth, int controlWidth, int unitWidth = 0);
    ~ParameterControl() override;

    /** Re-reads the control from the parameter. Never fires onChange. */
    void refresh();

    /** Greys the control and stops it being edited. Used where a parameter is
        registered but does not apply to the current mode — visible, so the panel
        still documents that it exists, but plainly not in play. */
    void setActive (bool active);

    /** Called after a successful write, for panels that must repaint or relayout
        in response. Not called when the value was unchanged. */
    std::function<void()> onChange;

    Parameter* getParameter() const { return m_parameter; }

    int getDesiredWidth() const;

    void resized() override;

private:
    void commit();

    Parameter* m_parameter = nullptr;

    int m_nameWidth = 0;
    int m_controlWidth = 0;
    int m_unitWidth = 0;

    std::unique_ptr<juce::Label> m_nameLabel;
    std::unique_ptr<juce::ComboBox> m_combo;
    std::unique_ptr<juce::Label> m_valueLabel;
    std::unique_ptr<juce::Label> m_unitLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterControl)
};

} // namespace TriggeredSpectra
