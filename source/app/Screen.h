#pragma once

#include "Session.h"
#include "Theme.h"
#include "Widgets.h"

namespace studio::ui
{

/** Base for every screen: knows the session and refreshes when the project changes. */
class Screen : public juce::Component, private juce::ChangeListener
{
public:
    explicit Screen (Session& s) : session (s) { session.addChangeListener (this); }
    ~Screen() override { session.removeChangeListener (this); }

    /** Rebuild from the project. `structural` is false for live control changes (faders etc.). */
    virtual void refresh (bool structural) = 0;
    virtual juce::String title() const = 0;
    /** One-line help shown under the title (and read by screen readers). */
    virtual juce::String subtitle() const { return {}; }

    void visibilityChanged() override { if (isVisible()) refresh (true); }

    /** Navigation request to another screen by id (set by the main component). */
    std::function<void (const juce::String&)> navigate;

protected:
    Session& session;

    /** Shows a modal-free message box. */
    static void tell (const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, title, message);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (isShowing()) refresh (session.lastChangeWasStructural);
    }
};

} // namespace studio::ui
