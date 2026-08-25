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

/** Whether a popup that just saw the focus move away should take it back.
 *
 *  PopupComponent::focusOfChildComponentChanged answers every focus change by
 *  calling grabKeyboardFocus() on itself, which is right while the popup is the
 *  only thing on screen and wrong in two cases:
 *
 *   - something the popup opened is on top of it (isOpenOver, above);
 *   - the window manager has just given the focus to another application.
 *
 *  The second one is what makes a call-out survive Alt+Tab on X11. A call-out is
 *  an override-redirect window, so the window manager cannot stack anything over
 *  it; JUCE's answer is CallOutBoxCallback's timer, which dismisses the box as
 *  soon as Process::isForegroundProcess() goes false. But the focus-out that
 *  should make it go false arrives as a focus change here first, and the grab
 *  puts the X input focus straight back -- LinuxComponentPeer::grabFocus() then
 *  sets isActiveApplication = true again, so JUCE never sees the application
 *  leave the foreground and the box stays up, painted over whatever the user
 *  switched to. Declining to grab lets the dismissal happen.
 */
inline bool shouldTakeKeyboardFocusBack (const juce::Component& popupContent)
{
    return juce::Process::isForegroundProcess() && ! isOpenOver (popupContent);
}

/** Opens `content` in a call-out anchored to `anchor`, guarded the same way as
 *  the class comment above describes: the call-out's window never takes the
 *  keyboard focus, so a click inside it cannot be misread as a click outside
 *  the popup that owns it, and the popup gets the focus back once `content`
 *  closes. Ownership of `content` passes to the CallOutBox.
 *
 *  Generic across whatever `content` needs of its own -- a colour selector's
 *  showColourPicker() below sets its size and colour before handing it here;
 *  a settings panel would do the same for its own state. */
inline void show (juce::Component& anchor, std::unique_ptr<juce::Component> content)
{
    auto& box = juce::CallOutBox::launchAsynchronously (
        std::move (content), anchor.getScreenBounds(), nullptr);

    // See the class comment: this is what keeps the click from closing it.
    box.setMouseClickGrabsKeyboardFocus (false);

    // Hand the focus back when it closes: the popup needs it for Escape and
    // Ctrl+Z, and it stopped taking it for itself while this was up.
    if (auto* popup = anchor.findParentComponentOfClass<PopupComponent>())
    {
        juce::ModalComponentManager::getInstance()->attachCallback (
            &box,
            juce::ModalCallbackFunction::create (
                [safePopup = juce::Component::SafePointer<juce::Component> (popup)] (int)
                {
                    // Not if the application is on its way to the background:
                    // the popup would take the X input focus back from whatever
                    // the user switched to. See shouldTakeKeyboardFocusBack().
                    if (safePopup != nullptr && safePopup->isShowing()
                        && juce::Process::isForegroundProcess())
                        safePopup->grabKeyboardFocus();
                }));
    }
}

/** A yes/no question drawn inside a nested call-out, for use from a component
 *  that already lives in one.
 *
 *  AlertWindow (showOkCancelBox and friends) is the obvious way to ask, and it
 *  is the wrong one here. A call-out goes onto the desktop with JUCE's
 *  `windowIsTemporary` flag, which on X11 makes it an override-redirect window:
 *  the window manager does not stack it, so it sits above the ordinary managed
 *  window an AlertWindow creates no matter which of the two is modal. The alert
 *  ends up *behind* the call-out that asked the question -- readable only where
 *  the call-out does not cover it, and on Ubuntu/GNOME that is most of it. It
 *  happens to look right on Windows, where the alert is raised above the popup,
 *  which is why the arrangement survived this long.
 *
 *  Asking from another call-out keeps the question in the same class of window
 *  as the thing that asked it, so the stacking is settled on every platform.
 */
class ConfirmationCallOut : public juce::Component
{
public:
    ConfirmationCallOut (juce::String title,
                         juce::String message,
                         const juce::String& confirmText,
                         const juce::String& cancelText,
                         std::function<void()> onConfirm)
        : m_title (std::move (title)),
          m_message (std::move (message)),
          m_onConfirm (std::move (onConfirm))
    {
        m_confirmButton = std::make_unique<UtilityButton> (confirmText);
        m_confirmButton->onClick = [this] { dismissThen (m_onConfirm); };
        addAndMakeVisible (m_confirmButton.get());

        m_cancelButton = std::make_unique<UtilityButton> (cancelText);
        m_cancelButton->onClick = [this] { dismissThen ({}); };
        addAndMakeVisible (m_cancelButton.get());

        setSize (width, titleHeight + measureMessageHeight() + buttonRow + margin);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (findColour (ThemeColours::componentBackground));

        g.setColour (findColour (ThemeColours::defaultText));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (m_title,
                    margin,
                    margin,
                    width - 2 * margin,
                    titleHeight - margin,
                    juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (m_message,
                          margin,
                          titleHeight,
                          width - 2 * margin,
                          getHeight() - titleHeight - buttonRow,
                          juce::Justification::topLeft,
                          maxMessageLines);
    }

    void resized() override
    {
        auto row = getLocalBounds().removeFromBottom (buttonRow).reduced (margin, 4);

        m_cancelButton->setBounds (row.removeFromRight (80));
        row.removeFromRight (6);
        m_confirmButton->setBounds (row.removeFromRight (80));
    }

private:
    /** Exits this call-out first, then runs `action` on the next message, so the
        action is free to rebuild -- or dismiss -- whatever opened this. */
    void dismissThen (std::function<void()> action)
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->exitModalState (0);

        if (action)
            juce::MessageManager::callAsync (std::move (action));
    }

    int measureMessageHeight() const
    {
        juce::AttributedString text;
        text.append (m_message, juce::FontOptions (12.0f));

        juce::TextLayout layout;
        layout.createLayout (text, static_cast<float> (width - 2 * margin));

        return juce::jlimit (32, maxMessageLines * 16, static_cast<int> (layout.getHeight()) + 8);
    }

    static constexpr int width = 320;
    static constexpr int margin = 10;
    static constexpr int titleHeight = 34;
    static constexpr int buttonRow = 34;
    static constexpr int maxMessageLines = 10;

    const juce::String m_title;
    const juce::String m_message;
    std::function<void()> m_onConfirm;

    std::unique_ptr<UtilityButton> m_confirmButton;
    std::unique_ptr<UtilityButton> m_cancelButton;
};

/** Asks `message` in a call-out anchored to `anchor`, running `onConfirm` only
 *  if the confirm button is clicked. Clicking away is a cancel, as it is for
 *  every other call-out. See ConfirmationCallOut for why this is not an
 *  AlertWindow. */
inline void showConfirmation (juce::Component& anchor,
                              const juce::String& title,
                              const juce::String& message,
                              const juce::String& confirmText,
                              const juce::String& cancelText,
                              std::function<void()> onConfirm)
{
    show (anchor,
          std::make_unique<ConfirmationCallOut> (
              title, message, confirmText, cancelText, std::move (onConfirm)));
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

    show (anchor, std::move (selector));
}

} // namespace EventTriggered::NestedCallOut

namespace EventTriggered
{

/** PopupComponent with NestedCallOut::shouldTakeKeyboardFocusBack() applied.
 *
 *  Every popup in this repo derives from this rather than from PopupComponent
 *  directly, because the base class's unconditional grab is wrong for all of
 *  them in the same two ways -- see shouldTakeKeyboardFocusBack() -- and a guard
 *  that has to be remembered per window is a guard that gets forgotten. It was:
 *  the trigger table and the pair table had it for their colour pickers, the
 *  other four did not, and all six kept themselves on top of whatever the user
 *  Alt+Tabbed to. */
class PopupWindow : public PopupComponent
{
public:
    using PopupComponent::PopupComponent;

    void focusOfChildComponentChanged (FocusChangeType cause) override
    {
        if (! NestedCallOut::shouldTakeKeyboardFocusBack (*this))
            return;

        PopupComponent::focusOfChildComponentChanged (cause);
    }
};

} // namespace EventTriggered
