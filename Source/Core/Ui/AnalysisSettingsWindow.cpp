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
#include "AnalysisSettingsWindow.h"

#include "../ParameterNames.h"
#include "../TriggeredSpectraNode.h"
#include "../Types.h"
#include "ParameterLayout.h"

#include <algorithm>

namespace TriggeredSpectra
{

AnalysisSettingsWindow::AnalysisSettingsWindow (TriggeredSpectraNode* node,
                                                bool acquisitionIsActive,
                                                juce::Component* anchor)
    : PopupComponent (anchor), m_node (node), m_acquisitionIsActive (acquisitionIsActive)
{
    jassert (anchor != nullptr); // PopupComponent dereferences it in its constructor

    // The groups are declared in ParameterLayout.h rather than here, so that the
    // editor/canvas split is stated once and can be checked by a test.
    for (const auto& group : ParameterLayout::analysisGroups)
        addSection (group.title, group.inactiveNote, group.names);

    refreshValues();
    applyEnablement();

    const int height = headerHeight + layoutSections (columnWidth * 2, false) + footerHeight;
    setSize (columnWidth * 2 + 24, height);
}

AnalysisSettingsWindow::~AnalysisSettingsWindow() = default;

// --- Construction ----------------------------------------------------------

AnalysisSettingsWindow::Section* AnalysisSettingsWindow::addSection (
    const juce::String& title,
    const juce::String& inactiveNote,
    const std::vector<const char*>& parameterNames)
{
    if (m_node == nullptr)
        return nullptr;

    auto section = std::make_unique<Section>();
    section->title = title;
    section->inactiveNote = inactiveNote;

    for (const auto& name : parameterNames)
    {
        auto* parameter = m_node->getParameter (name);

        if (parameter == nullptr)
            continue;

        auto control =
            std::make_unique<ParameterControl> (parameter, nameWidth, controlWidth, unitWidth);

        // A method selector decides which sibling sections apply, so the greying
        // has to be recomputed as soon as one changes.
        control->onChange = [this]
        {
            applyEnablement();
            repaint();
        };

        addAndMakeVisible (control.get());
        section->controls.push_back (std::move (control));
    }

    if (section->controls.empty())
        return nullptr;

    m_sections.push_back (std::move (section));
    return m_sections.back().get();
}

// --- Values ----------------------------------------------------------------

void AnalysisSettingsWindow::refreshValues()
{
    for (auto& section : m_sections)
        for (auto& control : section->controls)
            control->refresh();
}

// --- Enablement ------------------------------------------------------------

int AnalysisSettingsWindow::selectedIndexOf (const char* parameterName) const
{
    if (m_node == nullptr)
        return -1;

    auto* parameter = m_node->getParameter (parameterName);

    if (parameter == nullptr || parameter->getType() != Parameter::CATEGORICAL_PARAM)
        return -1;

    return static_cast<CategoricalParameter*> (parameter)->getSelectedIndex();
}

bool AnalysisSettingsWindow::isSpectrogramMode() const
{
    return m_node != nullptr && m_node->getEstimateMode() == EstimateMode::Spectrogram;
}

void AnalysisSettingsWindow::applyEnablement()
{
    const bool spectrogram = isSpectrogramMode();

    // 0 == Morlet, 1 == Hann STFT; 0 == Multitaper, 1 == Hann.
    const bool usingMorlet = selectedIndexOf (ParameterNames::tf_method) == 0;
    const bool usingMultitaper = selectedIndexOf (ParameterNames::line_method) == 0;

    for (auto& section : m_sections)
    {
        bool active = true;

        if (section->title == "Spectrogram - Morlet")
            active = spectrogram && usingMorlet;
        else if (section->title == "Spectrogram - Hann STFT")
            active = spectrogram && ! usingMorlet;
        else if (section->title == "Spectrum - line" || section->title == "Per-trial storage")
            active = ! spectrogram;

        section->active = active;

        for (auto& control : section->controls)
        {
            const juce::String name = control->getParameter() != nullptr
                                          ? control->getParameter()->getName()
                                          : juce::String();

            // tf_method stays live in Spectrogram mode even while its section is
            // greyed as the not-currently-selected estimator — otherwise there is
            // no way to switch back to it.
            const bool isTfMethod = name.equalsIgnoreCase (ParameterNames::tf_method);

            bool controlActive = active || (isTfMethod && spectrogram);

            // Tapers mean nothing to the Hann estimator.
            if (controlActive && ! spectrogram && ! usingMultitaper
                && (name.equalsIgnoreCase (ParameterNames::nw)
                    || name.equalsIgnoreCase (ParameterNames::n_tapers)))
                controlActive = false;

            // Everything here reshapes the accumulators and reallocates the ring
            // buffer, which is only safe with the audio thread stopped.
            control->setActive (controlActive && ! m_acquisitionIsActive);
        }
    }
}

// --- Layout ----------------------------------------------------------------

int AnalysisSettingsWindow::layoutSections (int width, bool applyBounds)
{
    // Two columns, filling the shorter one each time so the window stays close to
    // square rather than growing into a strip.
    const int columns = 2;
    const int usableColumnWidth = width / columns;

    int columnHeight[2] = { 0, 0 };

    for (auto& section : m_sections)
    {
        const int column = columnHeight[0] <= columnHeight[1] ? 0 : 1;

        const int height = sectionTitleHeight
                           + static_cast<int> (section->controls.size()) * rowHeight + sectionGap;

        if (applyBounds)
        {
            section->bounds = { column * usableColumnWidth,
                                headerHeight + columnHeight[column],
                                usableColumnWidth,
                                height };

            auto rowArea = section->bounds.reduced (10, 0);
            rowArea.removeFromTop (sectionTitleHeight);

            for (auto& control : section->controls)
                control->setBounds (rowArea.removeFromTop (rowHeight).reduced (0, 2));
        }

        columnHeight[column] += height;
    }

    return std::max (columnHeight[0], columnHeight[1]);
}

void AnalysisSettingsWindow::resized() { layoutSections (getWidth(), true); }

void AnalysisSettingsWindow::updatePopup()
{
    refreshValues();
    applyEnablement();
    repaint();
}

// --- Painting --------------------------------------------------------------

void AnalysisSettingsWindow::paint (juce::Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));

    // --- Header -------------------------------------------------------------
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (12, 0);

    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Analysis settings", header, juce::Justification::centredLeft);

    // --- Sections -----------------------------------------------------------
    for (const auto& section : m_sections)
    {
        auto titleArea = section->bounds.reduced (10, 0).removeFromTop (sectionTitleHeight);

        g.setColour (findColour (ThemeColours::defaultText).withAlpha (section->active ? 0.85f : 0.4f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (section->title, titleArea, juce::Justification::centredLeft);

        g.setColour (findColour (ThemeColours::outline).withAlpha (section->active ? 0.5f : 0.2f));
        g.fillRect (titleArea.getX(), titleArea.getBottom() - 3, titleArea.getWidth(), 1);

        // Say what would have to change, rather than leaving a greyed block
        // unexplained.
        if (! section->active && section->inactiveNote.isNotEmpty())
        {
            g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.4f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (section->inactiveNote,
                        titleArea.withX (titleArea.getX() + 120).withWidth (titleArea.getWidth() - 120),
                        juce::Justification::centredRight);
        }
    }

    // --- Footer -------------------------------------------------------------
    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (12, 0);

    g.setFont (juce::FontOptions (11.0f));

    if (m_acquisitionIsActive)
    {
        g.setColour (juce::Colours::orange);
        g.drawText ("Stop acquisition to change these - they reallocate buffers the audio "
                    "thread is reading.",
                    footer,
                    juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.6f));
        g.drawText ("Changing any of these clears accumulated spectra. Display-only settings "
                    "are on the canvas.",
                    footer,
                    juce::Justification::centredLeft);
    }
}

} // namespace TriggeredSpectra
