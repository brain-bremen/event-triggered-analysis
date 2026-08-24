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

#include <EditorHeaders.h>
#include <JuceHeader.h>

/** Opening a second call-out from inside a PopupComponent.
 *
 *  A PopupComponent lives in a modal CallOutBox whose window carries JUCE's
 *  `windowIsTemporary` style. Launching another call-out (the colour picker)
 *  from a cell inside it puts a second modal temporary window on top, and one
 *  Windows path then closes the picker on the first click:
 *
 *    - the picker's window is not the focused one (either it never took focus,
 *      or PopupComponent::focusOfChildComponentChanged pulled focus straight
 *      back to the popup after the picker opened);
 *    - clicking it therefore makes Windows activate it, and the popup's window
 *      receives WM_KILLFOCUS;
 *    - JUCE answers a WM_KILLFOCUS on a window blocked by a *temporary* modal
 *      component by calling inputAttemptWhenModal() on that component
 *      (juce_Windowing_windows.cpp, WM_KILLFOCUS -> sendInputAttemptWhenModalMessage);
 *    - the mouse is no longer over the swatch that opened the picker, so
 *      CallOutBox::inputAttemptWhenModal reads the click as "clicked outside"
 *      and does exitModalState + setVisible (false) — synchronously, before the
 *      mouse-down is ever delivered to the colour space.
 *
 *  The user-visible result is a picker that appears and then vanishes on the
 *  first click, having changed nothing.
 *
 *  Two guards, both aimed at the same thing — never let a click inside the
 *  nested call-out move the keyboard focus between windows:
 *
 *    - setMouseClickGrabsKeyboardFocus (false) on the picker's box, so its
 *      window answers WM_MOUSEACTIVATE with MA_NOACTIVATE. The window is not
 *      activated, nothing loses focus, and the click is still delivered
 *      (MA_NOACTIVATE keeps the message; only MA_NOACTIVATEANDEAT drops it).
 *      This is the same dodge juce::PopupMenu uses for its own windows, and it
 *      is why combo boxes inside these popups have always worked.
 *    - isOpenOver(), which the popup checks before grabbing the keyboard focus
 *      back from something it opened.
 *
 *  Note that the equivalent code in OnlinePSTH — which this plugin's table was
 *  derived from — has no such guard, so it is expected to close the same way on
 *  a GUI new enough to have PopupComponent::focusOfChildComponentChanged (added
 *  April 2024). If a picker there behaves, it is the GUI version differing, not
 *  the plugin. */
namespace EventTriggered::NestedCallOut
{

/** True while a call-out other than the one holding `popupContent` is modal —
 *  i.e. while something this popup opened is on top of it. */
inline bool isOpenOver (const juce::Component& popupContent)
{
    auto* modal = juce::Component::getCurrentlyModalComponent();

    return modal != nullptr && modal != popupContent.findParentComponentOfClass<juce::CallOutBox>();
}

/** Opens a colour picker anchored to `anchor`, reporting changes to `listener`.
 *
 *  Deliberately no `editableColour` option: the hex field it adds is a Label
 *  that wants the keyboard focus, and the picker's window does not take focus
 *  here, so it could be clicked into but never typed in. */
inline void showColourPicker (juce::Component& anchor,
                              juce::Colour initialColour,
                              juce::ChangeListener& listener)
{
    auto selector = std::make_unique<juce::ColourSelector> (
        juce::ColourSelector::showColourAtTop | juce::ColourSelector::showColourspace);

    selector->setCurrentColour (initialColour);
    selector->setSize (240, 280);
    selector->addChangeListener (&listener);

    auto& box = juce::CallOutBox::launchAsynchronously (
        std::move (selector), anchor.getScreenBounds(), nullptr);

    // See the note above: this is what keeps the click from closing the picker.
    box.setMouseClickGrabsKeyboardFocus (false);

    // Hand the focus back when the picker closes: the popup needs it for Escape
    // and Ctrl+Z, and it stopped taking it for itself while the picker was up.
    if (auto* popup = anchor.findParentComponentOfClass<PopupComponent>())
    {
        juce::ModalComponentManager::getInstance()->attachCallback (
            &box,
            juce::ModalCallbackFunction::create (
                [safePopup = juce::Component::SafePointer<juce::Component> (popup)] (int)
                {
                    if (safePopup != nullptr && safePopup->isShowing())
                        safePopup->grabKeyboardFocus();
                }));
    }
}

} // namespace EventTriggered::NestedCallOut
