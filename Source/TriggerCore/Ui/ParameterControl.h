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

namespace EventTriggered
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
    /** How a numeric parameter is presented. Ignored for categorical ones, which
        are always a combo box. */
    enum class Style
    {
        /** Type-in field. Right for a value that is set once and left alone. */
        Field,
        /** Drag slider with the value beside it, committing continuously.
         *
         *  For parameters that are *tuned* by eye against what is on screen —
         *  the whitening exponent being the case this exists for. A 0.1-step
         *  text field cannot do that: you cannot see the spectrum respond while
         *  you type. Only offered for display-time parameters, since committing
         *  on every drag step would otherwise reconfigure the analysis at frame
         *  rate. */
        Slider
    };

    /** @param nameWidth     width of the parameter's display name; 0 omits it
        @param controlWidth  width of the combo box, editable field or slider
        @param unitWidth     width of the trailing unit; 0 omits it */
    ParameterControl (Parameter* parameter,
                      int nameWidth,
                      int controlWidth,
                      int unitWidth = 0,
                      Style style = Style::Field);
    ~ParameterControl() override;

    /** Re-reads the control from the parameter. Never fires onChange. */
    void refresh();

    /** Shows a float parameter in a unit other than the one it is stored in.
     *
     *  Reading multiplies by `scale`, committing divides by it, and the clamp
     *  still happens in the parameter's own units against the parameter's own
     *  range -- so no display unit can widen or narrow what a parameter accepts,
     *  and a value typed just outside the range comes back as the range's edge
     *  written in the unit it was typed in.
     *
     *  The number of decimals adapts to the parameter's step *as displayed*:
     *  0.1 deg is about 1 mm and about 3.6 screen pixels, so the precision that
     *  reads well differs by unit.
     *
     *  `scale` must be finite and positive; anything else is ignored, since a
     *  zero scale would make every value read as zero and be uninvertible.
     *  `unitOverride` replaces the trailing unit label; passing an empty string
     *  restores the parameter's own unit. A scale of exactly 1.0 with no
     *  override restores the parameter's own formatting as well.
     *
     *  Only float parameters are transformed. Ints and categoricals are counts
     *  and choices, and neither has a unit to convert. */
    void setDisplayTransform (double scale, const juce::String& unitOverride = {});

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

    /** True when a transform other than the identity is in force. */
    bool hasDisplayTransform() const;

    /** How the transformed value is written: enough decimals for one step of the
        parameter to change the number shown. */
    juce::String formatDisplayValue (double value) const;

    Parameter* m_parameter = nullptr;

    double m_displayScale = 1.0;
    juce::String m_unitOverride;

    int m_nameWidth = 0;
    int m_controlWidth = 0;
    int m_unitWidth = 0;

    std::unique_ptr<juce::Label> m_nameLabel;
    std::unique_ptr<juce::ComboBox> m_combo;
    std::unique_ptr<juce::Label> m_valueLabel;
    std::unique_ptr<juce::Label> m_unitLabel;
    std::unique_ptr<juce::Slider> m_slider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterControl)
};

} // namespace EventTriggered
