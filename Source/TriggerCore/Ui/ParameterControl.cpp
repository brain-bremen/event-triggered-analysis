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
#include "ParameterControl.h"

namespace EventTriggered
{

ParameterControl::ParameterControl (Parameter* parameter,
                                    int nameWidth,
                                    int controlWidth,
                                    int unitWidth,
                                    Style style)
    : m_parameter (parameter),
      m_nameWidth (nameWidth),
      m_controlWidth (controlWidth),
      m_unitWidth (unitWidth)
{
    if (m_parameter == nullptr)
        return;

    if (m_nameWidth > 0)
    {
        m_nameLabel = std::make_unique<juce::Label> (juce::String(), parameter->getDisplayName());
        m_nameLabel->setJustificationType (juce::Justification::centredRight);
        m_nameLabel->setFont (juce::FontOptions (12.0f));
        m_nameLabel->setTooltip (parameter->getDescription());
        addAndMakeVisible (m_nameLabel.get());
    }

    if (parameter->getType() == Parameter::CATEGORICAL_PARAM)
    {
        m_combo = std::make_unique<juce::ComboBox> (parameter->getName());

        const auto& categories = static_cast<CategoricalParameter*> (parameter)->getCategories();

        for (int i = 0; i < categories.size(); ++i)
            m_combo->addItem (categories[i], i + 1);

        m_combo->setTooltip (parameter->getDescription());
        m_combo->onChange = [this] { commit(); };

        addAndMakeVisible (m_combo.get());
    }
    else if (style == Style::Slider
             && (parameter->getType() == Parameter::FLOAT_PARAM
                 || parameter->getType() == Parameter::INT_PARAM))
    {
        const bool isFloat = (parameter->getType() == Parameter::FLOAT_PARAM);

        m_slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                   juce::Slider::TextBoxRight);

        if (isFloat)
        {
            auto* floatParameter = static_cast<FloatParameter*> (parameter);
            m_slider->setRange (floatParameter->getMinValue(), floatParameter->getMaxValue(), 0.01);
        }
        else
        {
            auto* intParameter = static_cast<IntParameter*> (parameter);
            m_slider->setRange (intParameter->getMinValue(), intParameter->getMaxValue(), 1.0);
        }

        // Finer than the parameter's declared step on purpose: the step is a
        // sensible increment for typing, but tuning by eye wants the slider to
        // move continuously. The range is still the parameter's, so nothing
        // out of bounds can be produced.
        m_slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
        m_slider->setTooltip (parameter->getDescription());
        m_slider->onValueChange = [this] { commit(); };

        addAndMakeVisible (m_slider.get());
    }
    else if (parameter->getType() == Parameter::FLOAT_PARAM
             || parameter->getType() == Parameter::INT_PARAM)
    {
        m_valueLabel = std::make_unique<juce::Label> (juce::String(), juce::String());
        m_valueLabel->setEditable (false, true, false);
        m_valueLabel->setJustificationType (juce::Justification::centred);
        m_valueLabel->setColour (juce::Label::backgroundColourId,
                                 juce::Colours::white.withAlpha (0.1f));
        m_valueLabel->setColour (juce::Label::textColourId, juce::Colours::white);
        m_valueLabel->setFont (juce::FontOptions (12.0f));
        m_valueLabel->setTooltip (parameter->getDescription());
        m_valueLabel->onTextChange = [this] { commit(); };

        addAndMakeVisible (m_valueLabel.get());

        if (m_unitWidth > 0 && parameter->getType() == Parameter::FLOAT_PARAM)
        {
            const juce::String unit = static_cast<FloatParameter*> (parameter)->getUnit();

            if (unit.isNotEmpty())
            {
                m_unitLabel = std::make_unique<juce::Label> (juce::String(), unit);
                m_unitLabel->setFont (juce::FontOptions (10.0f));
                m_unitLabel->setJustificationType (juce::Justification::centredLeft);
                addAndMakeVisible (m_unitLabel.get());
            }
        }
    }

    refresh();
}

ParameterControl::~ParameterControl() = default;

void ParameterControl::refresh()
{
    if (m_parameter == nullptr)
        return;

    // dontSendNotification throughout: refreshing must never look like an edit,
    // or reading a value back would write it again.
    if (m_combo != nullptr)
    {
        const int index = static_cast<CategoricalParameter*> (m_parameter)->getSelectedIndex();
        m_combo->setSelectedId (index + 1, juce::dontSendNotification);
    }
    else if (m_slider != nullptr)
    {
        m_slider->setValue (m_parameter->getValue(), juce::dontSendNotification);
    }
    else if (m_valueLabel != nullptr)
    {
        m_valueLabel->setText (m_parameter->getValueAsString(), juce::dontSendNotification);
    }
}

void ParameterControl::commit()
{
    if (m_parameter == nullptr || ! isEnabled())
        return;

    bool changed = false;

    if (m_combo != nullptr)
    {
        const int index = m_combo->getSelectedId() - 1;

        if (index >= 0
            && index != static_cast<CategoricalParameter*> (m_parameter)->getSelectedIndex())
        {
            m_parameter->setNextValue (index);
            changed = true;
        }
    }
    else if (m_slider != nullptr)
    {
        if (m_parameter->getType() == Parameter::FLOAT_PARAM)
        {
            auto* parameter = static_cast<FloatParameter*> (m_parameter);
            const auto value = static_cast<float> (m_slider->getValue());

            changed = ! juce::approximatelyEqual (value, parameter->getFloatValue());

            if (changed)
                m_parameter->setNextValue (value);
        }
        else
        {
            auto* parameter = static_cast<IntParameter*> (m_parameter);
            const auto value = static_cast<int> (std::lround (m_slider->getValue()));

            changed = value != parameter->getIntValue();

            if (changed)
                m_parameter->setNextValue (value);
        }
    }
    else if (m_valueLabel != nullptr)
    {
        const juce::String text = m_valueLabel->getText();

        // Clamp rather than reject. The parameter's range is the authority, and a
        // silently discarded edit is worse than a corrected one; refresh() below
        // writes the accepted value back so the field cannot misreport what is set.
        if (m_parameter->getType() == Parameter::FLOAT_PARAM)
        {
            auto* parameter = static_cast<FloatParameter*> (m_parameter);
            const float value = juce::jlimit (
                parameter->getMinValue(), parameter->getMaxValue(), text.getFloatValue());

            changed = ! juce::approximatelyEqual (value, parameter->getFloatValue());

            if (changed)
                m_parameter->setNextValue (value);
        }
        else
        {
            auto* parameter = static_cast<IntParameter*> (m_parameter);
            const int value = juce::jlimit (
                parameter->getMinValue(), parameter->getMaxValue(), text.getIntValue());

            changed = value != parameter->getIntValue();

            if (changed)
                m_parameter->setNextValue (value);
        }

        refresh();
    }

    if (changed && onChange != nullptr)
        onChange();
}

void ParameterControl::setActive (bool active)
{
    setEnabled (active);

    if (m_nameLabel != nullptr)
        m_nameLabel->setAlpha (active ? 1.0f : 0.4f);
    if (m_unitLabel != nullptr)
        m_unitLabel->setAlpha (active ? 1.0f : 0.4f);

    if (m_combo != nullptr)
        m_combo->setEnabled (active);

    if (m_slider != nullptr)
    {
        m_slider->setEnabled (active);
        m_slider->setAlpha (active ? 1.0f : 0.4f);
    }

    if (m_valueLabel != nullptr)
    {
        m_valueLabel->setEnabled (active);
        m_valueLabel->setEditable (false, active, false);
    }
}

int ParameterControl::getDesiredWidth() const
{
    int width = m_controlWidth;

    if (m_nameWidth > 0)
        width += m_nameWidth + 6;
    if (m_unitLabel != nullptr)
        width += m_unitWidth + 3;

    return width;
}

void ParameterControl::resized()
{
    auto bounds = getLocalBounds();

    if (m_nameLabel != nullptr)
    {
        m_nameLabel->setBounds (bounds.removeFromLeft (m_nameWidth));
        bounds.removeFromLeft (6);
    }

    if (m_combo != nullptr)
        m_combo->setBounds (bounds.removeFromLeft (m_controlWidth));
    else if (m_slider != nullptr)
        m_slider->setBounds (bounds.removeFromLeft (m_controlWidth));
    else if (m_valueLabel != nullptr)
        m_valueLabel->setBounds (bounds.removeFromLeft (m_controlWidth));

    if (m_unitLabel != nullptr)
    {
        bounds.removeFromLeft (3);
        m_unitLabel->setBounds (bounds.removeFromLeft (m_unitWidth));
    }
}

} // namespace EventTriggered
