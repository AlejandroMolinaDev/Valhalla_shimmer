#include "PresetBar.h"

#include "LookAndFeel.h"

namespace ab::ui
{

PresetBar::PresetBar (PresetManager& manager) : presets (manager)
{
    previous.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    name.setTooltip ("Browse factory and user presets");
    save.setTooltip ("Save the current settings as a user preset (Documents/Aurora Bloom/Presets)");
    slotA.setTooltip ("A/B comparison: switch to slot A");
    slotB.setTooltip ("A/B comparison: switch to slot B");
    copy.setTooltip ("Copy the current slot into the other one");

    previous.setTitle ("Previous preset");
    next.setTitle ("Next preset");
    name.setTitle ("Preset menu");

    previous.setConnectedEdges (juce::Button::ConnectedOnRight);
    name.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    next.setConnectedEdges (juce::Button::ConnectedOnLeft);
    slotA.setConnectedEdges (juce::Button::ConnectedOnRight);
    slotB.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    copy.setConnectedEdges (juce::Button::ConnectedOnLeft);

    previous.onClick = [this] { presets.loadPrevious(); };
    next.onClick = [this] { presets.loadNext(); };
    name.onClick = [this] { showPresetMenu(); };
    save.onClick = [this] { showSaveDialog(); };
    slotA.onClick = [this] { if (presets.getActiveSlot() != 0) presets.toggleSlot(); };
    slotB.onClick = [this] { if (presets.getActiveSlot() != 1) presets.toggleSlot(); };
    copy.onClick = [this] { presets.copyToOtherSlot(); };

    for (auto* b : { &previous, &next, &name, &save, &slotA, &slotB, &copy })
    {
        b->setWantsKeyboardFocus (true);
        b->setMouseClickGrabsKeyboardFocus (false); // keep FL Studio's keyboard shortcuts working
        addAndMakeVisible (b);
    }

    presets.addChangeListener (this);
    updateDisplay();
    startTimerHz (8); // picks up the "modified" flag set from the audio/automation side
}

PresetBar::~PresetBar()
{
    presets.removeChangeListener (this);
}

void PresetBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateDisplay();
}

void PresetBar::timerCallback()
{
    if (presets.isModified() != lastModified)
        updateDisplay();
}

void PresetBar::updateDisplay()
{
    lastModified = presets.isModified();
    name.setButtonText (presets.getCurrentName() + (lastModified ? " *" : ""));
    slotA.setToggleState (presets.getActiveSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState (presets.getActiveSlot() == 1, juce::dontSendNotification);
}

void PresetBar::resized()
{
    auto b = getLocalBounds();
    const int gap = 8;

    copy.setBounds (b.removeFromRight (48));
    slotB.setBounds (b.removeFromRight (28).withTrimmedRight (-1));
    slotA.setBounds (b.removeFromRight (28).withTrimmedRight (-1));
    b.removeFromRight (gap);
    save.setBounds (b.removeFromRight (54));
    b.removeFromRight (gap);
    next.setBounds (b.removeFromRight (28));
    previous.setBounds (b.removeFromLeft (28).withTrimmedRight (-1));
    name.setBounds (b.withTrimmedRight (-1));
}

void PresetBar::showPresetMenu()
{
    presets.refresh();
    const auto& list = presets.getPresets();
    const int current = presets.getCurrentIndex();

    juce::PopupMenu menu;
    menu.addItem (10001, "Init (default settings)", true, current < 0 && presets.getCurrentName() == "Init");
    menu.addSeparator();
    juce::String lastCategory;
    bool userHeaderAdded = false;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& p = list[i];
        if (! p.isFactory && ! userHeaderAdded)
        {
            menu.addSeparator();
            menu.addSectionHeader ("User");
            userHeaderAdded = true;
        }
        else if (p.isFactory && p.category != lastCategory)
        {
            menu.addSectionHeader (p.category);
            lastCategory = p.category;
        }
        menu.addItem (static_cast<int> (i) + 1, p.name, true, static_cast<int> (i) == current);
    }
    menu.addSeparator();
    menu.addItem (10000, "Open user preset folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name).withMinimumWidth (name.getWidth()),
                        [safe = juce::Component::SafePointer<PresetBar> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 10001)
                            {
                                safe->presets.loadInit();
                                return;
                            }
                            if (result == 10000)
                            {
                                const auto folder = PresetManager::getUserPresetFolder();
                                folder.createDirectory();
                                folder.startAsProcess();
                                return;
                            }
                            safe->presets.loadPreset (result - 1);
                        });
}

void PresetBar::showSaveDialog()
{
    auto* window = new juce::AlertWindow ("Save preset", "Name of the new user preset:",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", presets.getCurrentName() == "Init" ? juce::String ("My Preset")
                                                                      : presets.getCurrentName());
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
                             juce::ModalCallbackFunction::create (
                                 [safe = juce::Component::SafePointer<PresetBar> (this), window] (int result)
                                 {
                                     if (safe == nullptr || result != 1)
                                         return;

                                     const auto presetName = window->getTextEditorContents ("name").trim();
                                     if (presetName.isEmpty())
                                         return;

                                     if (! safe->presets.userPresetExists (presetName))
                                     {
                                         safe->presets.saveUserPreset (presetName);
                                         return;
                                     }

                                     // Never overwrite an existing user preset without asking.
                                     juce::AlertWindow::showAsync (
                                         juce::MessageBoxOptions()
                                             .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                             .withTitle ("Replace preset?")
                                             .withMessage ("A user preset called \"" + presetName
                                                           + "\" already exists. Replace it?")
                                             .withButton ("Replace")
                                             .withButton ("Cancel")
                                             .withAssociatedComponent (safe.getComponent()),
                                         [safe, presetName] (int answer)
                                         {
                                             if (safe != nullptr && answer == 1)
                                                 safe->presets.saveUserPreset (presetName);
                                         });
                                 }),
                             true);
}

} // namespace ab::ui
