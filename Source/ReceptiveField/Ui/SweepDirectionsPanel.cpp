/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Receptive Field Mapper
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
#include "SweepDirectionsPanel.h"

#include "../BarMapperNode.h"

#include "TriggerCore/TriggerSource.h"

#include <cmath>
#include <utility>

using namespace juce;

namespace EventTriggered
{

namespace
{
    constexpr int rowHeight = 24;
    constexpr int headerHeight = 30;
    constexpr int nameWidth = 70;
    constexpr int patternWidth = 220;
    constexpr int angleWidth = 70;
    constexpr int windowWidth = nameWidth + patternWidth + angleWidth + 40;

    /** The generator block: count, trigger, arm message. */
    constexpr int generatorRows = 3;

    /** Two lines, because a full base message plus the first and last pattern
        does not fit on one. */
    constexpr int previewHeight = 32;

    constexpr int warningHeight = 16;
    constexpr int bottomMargin = 10;

    juce::String arrow()
    {
        // Explicit code point, for the same reason the degree sign in
        // SweepAngles is one: the source file's encoding is not something a
        // build should have to be right about.
        return juce::String::charToString (static_cast<juce::juce_wchar> (0x2192));
    }
} // namespace

// --- Compass ---------------------------------------------------------------

void CompassPreview::setArrows (std::vector<Arrow> arrows)
{
    m_arrows = std::move (arrows);
    repaint();
}

void CompassPreview::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    const Point<float> centre = bounds.getCentre();
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f - 12.0f;

    g.setColour (Colours::darkgrey);
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    if (m_arrows.empty())
    {
        g.setColour (Colours::grey);
        g.setFont (FontOptions (11.0f));
        g.drawText ("no angles set", getLocalBounds(), Justification::centred, true);
        return;
    }

    for (const Arrow& arrow : m_arrows)
    {
        const double rad = Rf::degToRad (arrow.canonicalAngleDeg);

        // Screen y grows downwards, visual-field y upwards. Flipped here so the
        // compass agrees with the map; drawn the other way it would show every
        // direction mirrored, which is exactly the class of error it exists to
        // expose.
        const Point<float> tip (centre.x + radius * static_cast<float> (std::cos (rad)),
                                centre.y - radius * static_cast<float> (std::sin (rad)));

        g.setColour (arrow.colour);
        g.drawArrow (Line<float> (centre, tip), 1.5f, 6.0f, 8.0f);

        const Point<float> labelAt (
            centre.x + (radius + 10.0f) * static_cast<float> (std::cos (rad)),
            centre.y - (radius + 10.0f) * static_cast<float> (std::sin (rad)));

        g.setFont (FontOptions (10.0f));
        g.drawText (arrow.label,
                    Rectangle<float> (34.0f, 12.0f).withCentre (labelAt),
                    Justification::centred,
                    false);
    }
}

// --- Shared with RfAnalysisSettingsWindow's inline compass ------------------

std::vector<CompassPreview::Arrow> buildCompassArrows (BarMapperNode& node)
{
    const Rf::AngleConvention convention = node.getAngleConvention();

    std::vector<CompassPreview::Arrow> arrows;

    for (TriggerSource* source : node.getTriggerSources().getAll())
    {
        const auto angle = node.getSweepAngles().getAngleDeg (source);

        if (! angle.has_value())
            continue;

        arrows.push_back ({ Rf::toCanonicalDeg (*angle, convention), source->name, source->colour });
    }

    return arrows;
}

String describeAngleWarnings (BarMapperNode& node)
{
    String warnings;

    for (const Rf::AngleSetWarning warning : node.checkAngles())
        warnings += (warnings.isEmpty() ? "" : "; ") + String (Rf::describe (warning));

    return warnings;
}

// --- Panel -------------------------------------------------------------

SweepDirectionsPanel::SweepDirectionsPanel (BarMapperNode* node, bool acquisitionIsActive)
    : m_node (node), m_acquisitionIsActive (acquisitionIsActive)
{
    const auto makeLabel = [this] (const String& text)
    {
        auto label = std::make_unique<Label> (text, text);
        label->setFont (FontOptions (12.0f));
        addAndMakeVisible (label.get());
        return label;
    };

    m_conventionLabel = makeLabel ("Zero at / turns");

    m_zeroSelector = std::make_unique<ComboBox> ("zero");
    m_zeroSelector->addItemList ({ "Right", "Up", "Left", "Down" }, 1);
    m_zeroSelector->addListener (this);
    addAndMakeVisible (m_zeroSelector.get());

    m_senseSelector = std::make_unique<ComboBox> ("sense");
    m_senseSelector->addItemList ({ "CCW", "CW" }, 1);
    m_senseSelector->addListener (this);
    addAndMakeVisible (m_senseSelector.get());

    const auto makeEditable = [this] (const String& name, const String& tooltip)
    {
        auto label = std::make_unique<Label> (name, String());
        label->setEditable (true);
        label->setFont (FontOptions (12.0f));
        label->setColour (Label::backgroundColourId, Colours::black.withAlpha (0.4f));
        label->setColour (Label::textColourId, Colours::white);
        label->setTooltip (tooltip);
        label->addListener (this);
        addAndMakeVisible (label.get());
        return label;
    };

    m_generateLabel = makeLabel ("Generate");

    m_generateCount = std::make_unique<ComboBox> ("count");
    for (const int n : { 4, 6, 8, 12, 16 })
        m_generateCount->addItem (String (n) + " directions", n);
    m_generateCount->addListener (this);
    addAndMakeVisible (m_generateCount.get());

    m_generateButton = std::make_unique<UtilityButton> ("REPLACE");
    m_generateButton->addListener (this);
    addAndMakeVisible (m_generateButton.get());

    m_triggerLabel = makeLabel ("Trigger");
    m_triggerNumber = makeEditable ("trigger",
                                    "TTL line carrying sweep onset, numbered as in "
                                    "the trigger table");

    m_incrementTrigger = std::make_unique<ToggleButton> ("one line per direction");
    m_incrementTrigger->setTooltip ("Off: every direction is armed on this one line and told "
                                    "apart by its message. On: line, line+1, line+2, ...");
    m_incrementTrigger->addListener (this);
    addAndMakeVisible (m_incrementTrigger.get());

    m_armLabel = makeLabel ("Arm msg");
    m_armBase = makeEditable ("armBase", "Text before the number, e.g. \"VSTIM: TRIALTYPE \"");
    m_armNumber =
        makeEditable ("armNumber", "Number for the first direction; the rest step up by one");
    m_armSuffix = makeEditable ("armSuffix",
                                "Text after the number. The trailing boundary is what stops "
                                "\"TRIALTYPE 3\" from also matching \"TRIALTYPE 30\", and what "
                                "stops the trial-end message from re-arming the source. Clear "
                                "it only if your messages carry their own boundary.");

    m_previewLabel = makeLabel ("");
    m_previewLabel->setFont (FontOptions (11.0f));
    m_previewLabel->setColour (Label::textColourId, Colours::grey);

    m_warningLabel = makeLabel ("");
    m_warningLabel->setColour (Label::textColourId, Colours::orange);
    m_warningLabel->setFont (FontOptions (11.0f));

    refresh();
}

SweepDirectionsPanel::~SweepDirectionsPanel() = default;

void SweepDirectionsPanel::refresh()
{
    if (m_node == nullptr)
        return;

    const Rf::AngleConvention convention = m_node->getAngleConvention();
    m_zeroSelector->setSelectedId (static_cast<int> (convention.zero) + 1, dontSendNotification);
    m_senseSelector->setSelectedId (static_cast<int> (convention.sense) + 1, dontSendNotification);

    syncGeneratorControls();
    rebuildRows();
    refreshWarnings();

    setSize (windowWidth,
             headerHeight + rowHeight * (static_cast<int> (m_rows.size()) + 1 + generatorRows)
                 + previewHeight + warningHeight + bottomMargin);
    resized();
}

void SweepDirectionsPanel::syncGeneratorControls()
{
    const DirectionGeneratorSpec& spec = m_node->getDirectionGeneratorSpec();

    // setSelectedId only takes if the count is one the combo offers; a spec
    // loaded with some other count leaves the box showing what it showed, which
    // would then be silently generated instead. Add the value rather than lose
    // it.
    if (m_generateCount->indexOfItemId (spec.count) < 0 && spec.count > 0)
        m_generateCount->addItem (String (spec.count) + " directions", spec.count);

    m_generateCount->setSelectedId (spec.count, dontSendNotification);

    m_triggerNumber->setText (String (spec.firstTriggerNumber), dontSendNotification);
    m_incrementTrigger->setToggleState (spec.incrementTriggerNumber, dontSendNotification);
    m_armBase->setText (spec.armMessageBase, dontSendNotification);
    m_armNumber->setText (String (spec.firstArmNumber), dontSendNotification);
    m_armSuffix->setText (spec.armMessageSuffix, dontSendNotification);

    generatorSettingsChanged();
}

DirectionGeneratorSpec SweepDirectionsPanel::specFromControls() const
{
    DirectionGeneratorSpec spec = m_node->getDirectionGeneratorSpec();

    spec.count = m_generateCount->getSelectedId();

    // Clamped to the range the trigger table itself accepts, so the generator
    // cannot create a source on a line the rest of the plugin would reject.
    spec.firstTriggerNumber = jlimit (1, 256, m_triggerNumber->getText().getIntValue());
    spec.incrementTriggerNumber = m_incrementTrigger->getToggleState();

    // Not trimmed. The separating space in "TRIALTYPE " and the leading space in
    // " TIMESEQUENCE" are load-bearing -- they are what makes the pattern match
    // one number and one message -- and trimming them here would quietly break
    // every pattern the generator writes.
    spec.armMessageBase = m_armBase->getText();
    spec.firstArmNumber = m_armNumber->getText().getIntValue();
    spec.armMessageSuffix = m_armSuffix->getText();

    return spec;
}

void SweepDirectionsPanel::generatorSettingsChanged()
{
    const DirectionGeneratorSpec spec = specFromControls();
    m_node->setDirectionGeneratorSpec (spec);

    const std::vector<GeneratedDirection> directions = generateDirections (spec);

    if (directions.empty())
    {
        m_previewLabel->setText (String(), dontSendNotification);
        return;
    }

    const GeneratedDirection& first = directions.front();
    const GeneratedDirection& last = directions.back();

    String text = first.armPattern;

    if (directions.size() > 1)
        text += "  " + arrow() + "  " + last.armPattern;

    text += spec.incrementTriggerNumber && directions.size() > 1
                ? "   on TTL " + String (first.triggerNumber) + "-" + String (last.triggerNumber)
                : "   on TTL " + String (first.triggerNumber);

    m_previewLabel->setText (text, dontSendNotification);
}

void SweepDirectionsPanel::rebuildRows()
{
    m_rows.clear();

    for (TriggerSource* source : m_node->getTriggerSources().getAll())
    {
        Row row;
        row.source = source;

        row.name = std::make_unique<Label> ("name", source->name);
        row.name->setFont (FontOptions (12.0f));
        row.name->setColour (Label::textColourId, source->colour);
        addAndMakeVisible (row.name.get());

        // Shown read-only. The pattern is the plugin's business, not the user's:
        // it is generated to match VStim's trial-start message and getting it
        // subtly wrong -- a missing trailing boundary, say -- is exactly the
        // failure the generator exists to prevent. Showing it still matters,
        // because "which message arms this row" is the first question when a
        // condition never fires.
        row.armPattern = std::make_unique<Label> (
            "pattern",
            source->armPattern.isNotEmpty() ? source->armPattern : String ("(not gated)"));
        row.armPattern->setFont (FontOptions (11.0f));
        row.armPattern->setColour (Label::textColourId, Colours::grey);
        addAndMakeVisible (row.armPattern.get());

        const auto angle = m_node->getSweepAngles().getAngleDeg (source);

        row.angle = std::make_unique<Label> ("angle", angle ? String (*angle, 1) : String());
        row.angle->setEditable (true);
        row.angle->setFont (FontOptions (12.0f));
        row.angle->setColour (Label::backgroundColourId, Colours::black.withAlpha (0.4f));
        row.angle->setColour (Label::textColourId, angle ? Colours::white : Colours::orange);
        row.angle->setTooltip ("Direction of motion in degrees, in the convention above");
        row.angle->addListener (this);
        addAndMakeVisible (row.angle.get());

        m_rows.push_back (std::move (row));
    }

    addAndMakeVisible (m_warningLabel.get());
}

void SweepDirectionsPanel::refreshWarnings()
{
    m_warningLabel->setText (describeAngleWarnings (*m_node), dontSendNotification);

    // Neither the warning line nor the compass lives in this popout; the
    // owner draws both, so it has to be told they might have changed.
    if (onChanged)
        onChanged();
}

void SweepDirectionsPanel::labelTextChanged (Label* label)
{
    if (label == m_triggerNumber.get() || label == m_armBase.get() || label == m_armNumber.get()
        || label == m_armSuffix.get())
    {
        generatorSettingsChanged();

        // The numeric boxes are clamped and parsed in specFromControls(), so
        // either can be left showing something the generator will not use --
        // "abc", or a line number out of range. Write the accepted values back
        // so the boxes say what will actually happen.
        const DirectionGeneratorSpec& spec = m_node->getDirectionGeneratorSpec();
        m_triggerNumber->setText (String (spec.firstTriggerNumber), dontSendNotification);
        m_armNumber->setText (String (spec.firstArmNumber), dontSendNotification);
        return;
    }

    for (const Row& row : m_rows)
    {
        if (row.angle.get() != label)
            continue;

        const String text = label->getText().trim();

        if (text.isEmpty())
            break;

        m_node->setAngleForSource (row.source, text.getDoubleValue());
        row.angle->setColour (Label::textColourId, Colours::white);
        break;
    }

    refreshWarnings();
}

void SweepDirectionsPanel::comboBoxChanged (ComboBox* box)
{
    const auto setParameter = [this] (const char* name, int index)
    {
        if (auto* parameter = m_node->getParameter (name))
            parameter->setNextValue (index);
    };

    if (box == m_generateCount.get())
    {
        generatorSettingsChanged();
        return;
    }

    if (box == m_zeroSelector.get())
        setParameter (RfParameterNames::angle_zero, box->getSelectedId() - 1);
    else if (box == m_senseSelector.get())
        setParameter (RfParameterNames::angle_sense, box->getSelectedId() - 1);
    else
        return;

    // The angles in the table do not change; what they *mean* does. Refreshing
    // here is what makes the owner's compass redraw to match.
    refreshWarnings();
}

void SweepDirectionsPanel::applyGeneratedDirections()
{
    m_node->generateDirectionSources (specFromControls());
    refresh();
}

void SweepDirectionsPanel::buttonClicked (Button* button)
{
    if (button == m_incrementTrigger.get())
    {
        generatorSettingsChanged();
        return;
    }

    if (button != m_generateButton.get())
        return;

    if (m_acquisitionIsActive)
        return;

    // Replaces every existing condition, so it asks first. A generator that
    // appended instead would leave the previous set in place with its own angles
    // and silently mix two stimulus sets into one map -- which is why it replaces,
    // and why replacing has to be deliberate.
    AlertWindow::showOkCancelBox (
        MessageBoxIconType::QuestionIcon,
        "Replace all conditions?",
        "This removes the current trigger sources and their accumulated trials, and "
        "creates "
            + String (m_generateCount->getSelectedId()) + " evenly spaced directions armed by\n\n"
            + m_previewLabel->getText(),
        "Replace",
        "Cancel",
        this,
        // SafePointer, not `this`: the popup this lives in can be dismissed while
        // the alert is still up -- clicking away from it is enough -- and the
        // callback then fires against a destroyed component.
        ModalCallbackFunction::create (
            [safe = Component::SafePointer<SweepDirectionsPanel> (this)] (int result)
            {
                if (result != 0 && safe != nullptr)
                    safe->applyGeneratedDirections();
            }));
}

void SweepDirectionsPanel::paint (Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));

    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (FontOptions (14.0f));
    g.drawText ("Sweep directions", 10, 6, windowWidth - 20, 20, Justification::centredLeft);

    // Drawn per column rather than as one padded string, so the headings stay
    // over their columns when a width changes.
    g.setFont (FontOptions (11.0f));
    g.setColour (Colours::grey);

    int x = 10;
    for (const auto& column : { std::pair { "Condition", nameWidth },
                                std::pair { "Armed by", patternWidth },
                                std::pair { "Angle", angleWidth } })
    {
        g.drawText (
            column.first, x, headerHeight - 2, column.second, 16, Justification::centredLeft);
        x += column.second;
    }
}

void SweepDirectionsPanel::resized()
{
    auto bounds = getLocalBounds().reduced (10, 0);
    bounds.removeFromTop (headerHeight + 16);

    for (const Row& row : m_rows)
    {
        auto line = bounds.removeFromTop (rowHeight);
        row.name->setBounds (line.removeFromLeft (nameWidth));
        row.armPattern->setBounds (line.removeFromLeft (patternWidth));
        row.angle->setBounds (line.removeFromLeft (angleWidth).reduced (2));
    }

    bounds.removeFromTop (6);

    auto conventionRow = bounds.removeFromTop (rowHeight);
    m_conventionLabel->setBounds (conventionRow.removeFromLeft (100));
    m_zeroSelector->setBounds (conventionRow.removeFromLeft (70).reduced (2, 1));
    m_senseSelector->setBounds (conventionRow.removeFromLeft (60).reduced (2, 1));

    auto generateRow = bounds.removeFromTop (rowHeight);
    m_generateLabel->setBounds (generateRow.removeFromLeft (60));
    m_generateCount->setBounds (generateRow.removeFromLeft (120).reduced (2, 1));
    m_generateButton->setBounds (generateRow.removeFromLeft (90).reduced (2, 1));

    auto triggerRow = bounds.removeFromTop (rowHeight);
    m_triggerLabel->setBounds (triggerRow.removeFromLeft (60));
    m_triggerNumber->setBounds (triggerRow.removeFromLeft (44).reduced (2, 2));
    triggerRow.removeFromLeft (6);
    m_incrementTrigger->setBounds (triggerRow);

    auto armRow = bounds.removeFromTop (rowHeight);
    m_armLabel->setBounds (armRow.removeFromLeft (60));
    m_armBase->setBounds (armRow.removeFromLeft (150).reduced (2, 2));
    m_armNumber->setBounds (armRow.removeFromLeft (50).reduced (2, 2));
    m_armSuffix->setBounds (armRow.reduced (2, 2));

    m_previewLabel->setBounds (bounds.removeFromTop (previewHeight));

    m_warningLabel->setBounds (bounds.removeFromTop (warningHeight));
}

} // namespace EventTriggered
