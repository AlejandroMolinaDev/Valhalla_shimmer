// GUI behaviour tests. They open the real editor (no host, no window) and drive every control
// the way a user does - drags, fine drags, the wheel, the keyboard, double clicks, the preset
// menu and buttons - then check the parameter, the host gestures and the display.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <set>
#include <thread>

#include "PluginEditor.h"
#include "PluginTestHelpers.h"

using namespace ab::plugintest;
namespace id = ab::params::id;
using Catch::Matchers::WithinAbs;

namespace
{
    const juce::StringArray kKnobIds { id::mix,     id::pitch,   id::feedback, id::diffusion, id::size,
                                       id::lowCut,  id::highCut, id::modRate,  id::modDepth };
    const juce::StringArray kSelectorIds { id::reverbMode, id::pitchMode, id::colorMode };

    juce::Component* findById (juce::Component& root, const juce::String& componentId)
    {
        if (root.getComponentID() == componentId)
            return &root;
        for (auto* child : root.getChildren())
            if (auto* found = findById (*child, componentId))
                return found;
        return nullptr;
    }

    template <typename T>
    T& find (juce::Component& root, const juce::String& componentId)
    {
        auto* c = dynamic_cast<T*> (findById (root, componentId));
        REQUIRE (c != nullptr);
        return *c;
    }

    /** Editor fixture: processor + editor at 100 % scale. */
    struct Gui
    {
        juce::ScopedJuceInitialiser_GUI init;
        AuroraBloomProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> editor;

        Gui()
        {
            prepareStereo (proc);
            editor.reset (proc.createEditor());
            REQUIRE (editor != nullptr);
            editor->setSize (AuroraBloomEditor::kBaseWidth, AuroraBloomEditor::kBaseHeight);
        }

        ~Gui() { editor.reset(); }

        ab::ui::KnobSlider& knob (const juce::String& paramId) { return find<ab::ui::KnobSlider> (*editor, paramId + "_slider"); }
        juce::ComboBox& selector (const juce::String& paramId) { return find<ab::ui::ModeSelector> (*editor, paramId).getComboBox(); }
        ab::ui::PresetBar& presetBar() { return find<ab::ui::PresetBar> (*editor, "presetBar"); }
        juce::TextButton& button (const juce::String& componentId) { return find<juce::TextButton> (*editor, componentId); }
        juce::RangedAudioParameter& param (const juce::String& paramId) { return *proc.getApvts().getParameter (paramId); }

        /** Delivers queued change messages (preset loads) without a running message loop. */
        void flush()
        {
            proc.getPresetManager().dispatchPendingMessages();
            presetBar().pollModified();
        }
    };

    juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos,
                            juce::ModifierKeys mods = {}, int clicks = 1, bool dragged = false)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &c, &c, now, downPos, now, clicks, dragged);
    }

    /** Presses on the knob centre, drags by (dx, dy) in steps of 1 px and releases. */
    void drag (juce::Slider& s, float dx, float dy, juce::ModifierKeys mods = {})
    {
        const auto start = s.getLocalBounds().getCentre().toFloat();
        const auto buttonMods = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
        s.mouseDown (mouse (s, start, start, buttonMods));
        const int steps = static_cast<int> (std::max (std::abs (dx), std::abs (dy)));
        for (int i = 1; i <= steps; ++i)
        {
            const auto t = static_cast<float> (i) / static_cast<float> (steps);
            s.mouseDrag (mouse (s, start + juce::Point<float> (dx * t, dy * t), start, buttonMods, 1, true));
        }
        s.mouseUp (mouse (s, start + juce::Point<float> (dx, dy), start, mods, 1, true));
    }

    void wheel (juce::Slider& s, float deltaY, juce::ModifierKeys mods = {})
    {
        juce::MouseWheelDetails w {};
        w.deltaX = 0.0f;
        w.deltaY = deltaY;
        w.isReversed = false;
        w.isSmooth = false;
        w.isInertial = false;
        const auto c = s.getLocalBounds().getCentre().toFloat();
        s.mouseWheelMove (mouse (s, c, c, mods), w);
    }

    double proportion (juce::Slider& s) { return s.valueToProportionOfLength (s.getValue()); }

    /** Counts begin/end gestures per parameter index (what FL Studio uses to record automation). */
    struct GestureSpy final : juce::AudioProcessorListener
    {
        std::map<int, int> begins, ends;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { ++begins[index]; }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { ++ends[index]; }
    };

    bool imagesDiffer (const juce::Image& a, const juce::Image& b)
    {
        if (a.getBounds() != b.getBounds())
            return true;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    return true;
        return false;
    }

    /** Number of distinct-looking pixels: guards against blank (single-colour) renders. */
    int colourVariety (const juce::Image& img)
    {
        std::set<juce::uint32> colours;
        for (int y = 0; y < img.getHeight(); y += 3)
            for (int x = 0; x < img.getWidth(); x += 3)
                colours.insert (img.getPixelAt (x, y).getARGB() & 0xf0f0f0f0u);
        return static_cast<int> (colours.size());
    }
} // namespace

// =============================================================================================
// Knobs
// =============================================================================================
TEST_CASE ("GUI: each knob is bound to its own parameter, both ways", "[gui][knobs]")
{
    Gui gui;
    for (const auto& knobId : kKnobIds)
    {
        INFO ("knob " << knobId);
        auto& s = gui.knob (knobId);
        auto& p = gui.param (knobId);

        // Same range and default as the parameter.
        CHECK_THAT (s.getMinimum(), WithinAbs (p.convertFrom0to1 (0.0f), 1.0e-4));
        CHECK_THAT (s.getMaximum(), WithinAbs (p.convertFrom0to1 (1.0f), 1.0e-3));
        CHECK_THAT (s.getDoubleClickReturnValue(), WithinAbs (p.convertFrom0to1 (p.getDefaultValue()), 1.0e-4));

        // Knob -> parameter, and nothing else moves.
        std::map<juce::String, float> before;
        for (const auto& other : ab::params::allIds())
            before[other] = getPlain (gui.proc, other);
        s.setValue (s.proportionOfLengthToValue (0.73), juce::sendNotificationSync);
        CHECK_THAT (p.getValue(), WithinAbs (0.73, 2.0e-3));
        for (const auto& other : ab::params::allIds())
            if (other != knobId)
                CHECK (getPlain (gui.proc, other) == before[other]);

        // Parameter (host automation) -> knob.
        p.setValueNotifyingHost (0.21f);
        CHECK_THAT (proportion (s), WithinAbs (0.21, 2.0e-3));

        // The value box shows the parameter's own text (units included).
        CHECK (s.getTextFromValue (s.getValue()) == p.getCurrentValueAsText());
    }
}

TEST_CASE ("GUI: value boxes show units and accept typed values", "[gui][knobs]")
{
    Gui gui;
    struct Typed { const char* knob; const char* text; double expected; };
    for (const auto& t : { Typed { id::mix, "35", 35.0 },         Typed { id::mix, "80 %", 80.0 },
                           Typed { id::pitch, "-7", -7.0 },        Typed { id::pitch, "+5 st", 5.0 },
                           Typed { id::feedback, "92.5", 92.5 },   Typed { id::diffusion, "0.62", 0.62 },
                           Typed { id::size, "40", 40.0 },         Typed { id::lowCut, "150 Hz", 150.0 },
                           Typed { id::lowCut, "1.2 kHz", 1200.0 }, Typed { id::highCut, "6k", 6000.0 },
                           Typed { id::highCut, "12 kHz", 12000.0 }, Typed { id::modRate, "2.5 Hz", 2.5 },
                           Typed { id::modDepth, "70", 70.0 } })
    {
        INFO (t.knob << " <- \"" << t.text << "\"");
        auto& s = gui.knob (t.knob);
        s.setValue (s.getValueFromText (t.text), juce::sendNotificationSync);
        CHECK_THAT (getPlain (gui.proc, t.knob), WithinAbs (t.expected, t.expected * 1.0e-3 + 0.011));
    }

    CHECK (gui.knob (id::mix).getTextFromValue (50.0).contains ("%"));
    CHECK (gui.knob (id::highCut).getTextFromValue (8000.0).containsIgnoreCase ("kHz"));
    CHECK (gui.knob (id::lowCut).getTextFromValue (100.0).containsIgnoreCase ("Hz"));
    CHECK (gui.knob (id::pitch).getTextFromValue (12.0).contains ("st"));
    CHECK (gui.knob (id::modRate).getTextFromValue (0.4).containsIgnoreCase ("Hz"));
}

TEST_CASE ("GUI: dragging a knob moves it, Ctrl/Cmd drags finely and the host sees one gesture", "[gui][knobs][mouse]")
{
    Gui gui;
    GestureSpy spy;
    gui.proc.addListener (&spy);

    auto& s = gui.knob (id::mix);
    auto& p = gui.param (id::mix);
    s.setValue (s.proportionOfLengthToValue (0.2), juce::sendNotificationSync);

    // 110 px upwards = half the range (220 px for the whole sweep).
    drag (s, 0.0f, -110.0f);
    CHECK_THAT (proportion (s), WithinAbs (0.7, 0.01));
    CHECK_THAT (p.getValue(), WithinAbs (0.7, 0.01));
    CHECK (spy.begins[p.getParameterIndex()] == 1);
    CHECK (spy.ends[p.getParameterIndex()] == 1);

    // Dragging down or left decreases; it clamps at the ends.
    drag (s, -40.0f, 300.0f);
    CHECK_THAT (proportion (s), WithinAbs (0.0, 1.0e-6));
    drag (s, 0.0f, -500.0f);
    CHECK_THAT (proportion (s), WithinAbs (1.0, 1.0e-6));

    // Fine mode: the same movement covers six times less.
    s.setValue (s.proportionOfLengthToValue (0.5), juce::sendNotificationSync);
    drag (s, 0.0f, -66.0f, juce::ModifierKeys (juce::ModifierKeys::commandModifier));
    CHECK_THAT (proportion (s), WithinAbs (0.5 + 66.0 / 220.0 / 6.0, 0.005));

    // Every knob answers a drag.
    for (const auto& knobId : kKnobIds)
    {
        auto& k = gui.knob (knobId);
        k.setValue (k.proportionOfLengthToValue (0.3), juce::sendNotificationSync);
        drag (k, 0.0f, -44.0f);
        INFO ("knob " << knobId);
        CHECK (proportion (k) > 0.45);
        CHECK (spy.ends[gui.param (knobId).getParameterIndex()] >= 1);
    }

    CHECK (spy.begins == spy.ends); // no gesture left open
    gui.proc.removeListener (&spy);
}

TEST_CASE ("GUI: the Pitch knob snaps to semitones; Shift gives 0.01 st steps", "[gui][knobs][mouse]")
{
    Gui gui;
    auto& s = gui.knob (id::pitch);
    s.setValue (0.0, juce::sendNotificationSync);

    for (const float dy : { -13.0f, -27.0f, 31.0f, -80.0f })
    {
        drag (s, 0.0f, dy);
        INFO ("after drag " << dy << ": " << s.getValue());
        CHECK_THAT (s.getValue(), WithinAbs (std::round (s.getValue()), 1.0e-6));
    }

    s.setValue (3.0, juce::sendNotificationSync);
    drag (s, 0.0f, -5.0f, juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
    CHECK (s.getValue() > 3.0);
    CHECK (s.getValue() < 3.5);
    CHECK (std::abs (s.getValue() - std::round (s.getValue())) > 1.0e-3); // fractional on purpose

    // Arrow keys move by a whole semitone; Shift + arrow by a fine step.
    s.setValue (2.0, juce::sendNotificationSync);
    s.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    CHECK_THAT (s.getValue(), WithinAbs (3.0, 1.0e-6));
    s.keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    s.keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    CHECK_THAT (s.getValue(), WithinAbs (1.0, 1.0e-6));
}

TEST_CASE ("GUI: double click, mouse wheel and keyboard on the knobs", "[gui][knobs][keyboard]")
{
    Gui gui;
    for (const auto& knobId : kKnobIds)
    {
        INFO ("knob " << knobId);
        auto& s = gui.knob (knobId);
        auto& p = gui.param (knobId);
        const auto c = s.getLocalBounds().getCentre().toFloat();

        // Double click: back to the default.
        s.setValue (s.proportionOfLengthToValue (0.95), juce::sendNotificationSync);
        s.mouseDoubleClick (mouse (s, c, c, {}, 2));
        CHECK_THAT (p.getValue(), WithinAbs (p.getDefaultValue(), 1.0e-3));

        // Wheel: up increases, down decreases, Ctrl/Cmd is finer.
        s.setValue (s.proportionOfLengthToValue (0.5), juce::sendNotificationSync);
        const bool snaps = knobId == juce::String (id::pitch);
        wheel (s, 0.1f);
        const auto afterUp = proportion (s);
        CHECK (afterUp > 0.5);
        wheel (s, -0.1f);
        CHECK_THAT (proportion (s), WithinAbs (0.5, snaps ? 0.03 : 1.0e-3));
        if (! snaps)
        {
            s.setValue (s.proportionOfLengthToValue (0.5), juce::sendNotificationSync);
            wheel (s, 0.1f, juce::ModifierKeys (juce::ModifierKeys::commandModifier));
            CHECK (proportion (s) - 0.5 < (afterUp - 0.5) * 0.5);
        }

        // Keyboard: Home / End / Delete (default) / arrows / Page keys.
        s.keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
        CHECK_THAT (proportion (s), WithinAbs (0.0, 1.0e-6));
        s.keyPressed (juce::KeyPress (juce::KeyPress::endKey));
        CHECK_THAT (proportion (s), WithinAbs (1.0, 1.0e-6));
        s.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
        CHECK_THAT (p.getValue(), WithinAbs (p.getDefaultValue(), 1.0e-3));
        if (! snaps)
        {
            s.setValue (s.proportionOfLengthToValue (0.5), juce::sendNotificationSync);
            s.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
            CHECK_THAT (proportion (s), WithinAbs (0.51, 2.0e-3));
            s.keyPressed (juce::KeyPress (juce::KeyPress::pageDownKey));
            CHECK_THAT (proportion (s), WithinAbs (0.41, 2.0e-3));
        }
    }
}

// =============================================================================================
// Mode selectors
// =============================================================================================
TEST_CASE ("GUI: mode selectors list the parameter's options in order and stay in sync", "[gui][selectors]")
{
    Gui gui;
    auto& canvas = find<juce::Component> (*gui.editor, "canvas");

    for (const auto& selectorId : kSelectorIds)
    {
        INFO ("selector " << selectorId);
        auto& box = gui.selector (selectorId);
        auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&gui.param (selectorId));
        REQUIRE (choice != nullptr);
        REQUIRE (box.getNumItems() == choice->choices.size());
        for (int i = 0; i < box.getNumItems(); ++i)
            CHECK (box.getItemText (i).equalsIgnoreCase (choice->choices[i]));

        CHECK (box.getSelectedItemIndex() == choice->getIndex());

        for (int i = 0; i < box.getNumItems(); ++i)
        {
            // GUI -> parameter
            box.setSelectedItemIndex (i, juce::sendNotificationSync);
            CHECK (choice->getIndex() == i);

            // Every selector stays fully inside the canvas and never overlaps another one.
            std::vector<juce::Rectangle<int>> boundsList;
            for (const auto& other : kSelectorIds)
            {
                auto& sel = find<ab::ui::ModeSelector> (*gui.editor, other);
                CHECK (canvas.getLocalBounds().contains (sel.getBounds()));
                CHECK (sel.getWidth() >= sel.getIdealWidth());
                boundsList.push_back (sel.getBounds());
            }
            CHECK_FALSE (boundsList[0].intersects (boundsList[1]));
            CHECK_FALSE (boundsList[1].intersects (boundsList[2]));
        }

        // Parameter (automation / preset) -> GUI
        for (int i = choice->choices.size() - 1; i >= 0; --i)
        {
            choice->setValueNotifyingHost (choice->convertTo0to1 (static_cast<float> (i)));
            CHECK (box.getSelectedItemIndex() == i);
        }
    }
}

// =============================================================================================
// Preset bar
// =============================================================================================
TEST_CASE ("GUI: preset bar - previous / next, menu, Init and the modified marker", "[gui][presets]")
{
    Gui gui;
    auto& manager = gui.proc.getPresetManager();
    auto& name = gui.button ("presetName");
    const int numFactory = manager.getNumFactoryPresets();
    REQUIRE (numFactory == 12);

    // Next walks forward through the list and wraps around.
    manager.loadPreset (0);
    gui.flush();
    CHECK (name.getButtonText() == manager.getPresets()[0].name);
    gui.button ("presetNext").onClick();
    gui.flush();
    CHECK (name.getButtonText() == manager.getPresets()[1].name);
    gui.button ("presetPrevious").onClick();
    gui.button ("presetPrevious").onClick();
    gui.flush();
    CHECK (name.getButtonText() == manager.getPresets().back().name);

    // The menu: Init, every factory preset (ticked when current) and the folder entry.
    manager.loadPreset (3);
    auto menu = gui.presetBar().buildPresetMenu();
    std::map<int, juce::String> items;
    int ticked = -1;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        const auto& item = it.getItem();
        if (item.itemID != 0)
            items[item.itemID] = item.text;
        if (item.isTicked)
            ticked = item.itemID;
    }
    CHECK (items.count (ab::ui::PresetBar::kInitItem) == 1);
    CHECK (items.count (ab::ui::PresetBar::kOpenFolderItem) == 1);
    for (int i = 0; i < numFactory; ++i)
        CHECK (items[i + 1] == manager.getPresets()[static_cast<size_t> (i)].name);
    CHECK (ticked == 4);

    // Choosing an entry loads it, parameters included.
    gui.presetBar().handlePresetMenuResult (10); // "Endless Pad" is the 10th preset
    gui.flush();
    CHECK (name.getButtonText() == "Endless Pad");
    CHECK_THAT (getPlain (gui.proc, id::feedback), WithinAbs (100.0, 0.01));
    CHECK (gui.knob (id::feedback).getValue() == 100.0);

    // Touching a knob marks the preset as modified ("*"); loading clears it.
    gui.knob (id::size).setValue (12.0, juce::sendNotificationSync);
    gui.flush();
    CHECK (name.getButtonText() == "Endless Pad *");

    gui.presetBar().handlePresetMenuResult (ab::ui::PresetBar::kInitItem);
    gui.flush();
    CHECK (name.getButtonText() == "Init");
    for (const auto& paramId : ab::params::allIds())
        CHECK_THAT (gui.param (paramId).getValue(), WithinAbs (gui.param (paramId).getDefaultValue(), 1.0e-4));

    // Dismissing the menu does nothing.
    gui.presetBar().handlePresetMenuResult (0);
    gui.flush();
    CHECK (name.getButtonText() == "Init");
}

TEST_CASE ("GUI: A/B buttons swap settings, Copy duplicates the active slot", "[gui][presets]")
{
    Gui gui;
    auto& a = gui.button ("slotA");
    auto& b = gui.button ("slotB");
    gui.flush();
    CHECK (a.getToggleState());
    CHECK_FALSE (b.getToggleState());

    auto& mixKnob = gui.knob (id::mix);
    mixKnob.setValue (20.0, juce::sendNotificationSync);

    b.onClick();
    gui.flush();
    CHECK (b.getToggleState());
    CHECK_FALSE (a.getToggleState());
    CHECK_THAT (mixKnob.getValue(), WithinAbs (20.0, 0.01)); // B starts as a copy of A
    mixKnob.setValue (90.0, juce::sendNotificationSync);

    a.onClick();
    gui.flush();
    CHECK_THAT (mixKnob.getValue(), WithinAbs (20.0, 0.01));
    CHECK_THAT (getPlain (gui.proc, id::mix), WithinAbs (20.0, 0.01));
    b.onClick();
    gui.flush();
    CHECK_THAT (mixKnob.getValue(), WithinAbs (90.0, 0.01));

    // Copy B -> A, then A holds 90 too.
    gui.button ("slotCopy").onClick();
    a.onClick();
    gui.flush();
    CHECK_THAT (mixKnob.getValue(), WithinAbs (90.0, 0.01));
}

TEST_CASE ("GUI: user presets save, appear in the menu, reload and can be replaced", "[gui][presets]")
{
    Gui gui;
    auto& manager = gui.proc.getPresetManager();
    const auto presetName = "zz Test " + juce::String (juce::Time::currentTimeMillis());
    const auto file = PresetManager::getUserPresetFolder().getChildFile (presetName + ".xml");

    CHECK_FALSE (manager.userPresetExists (presetName));
    gui.knob (id::pitch).setValue (-5.0, juce::sendNotificationSync);
    gui.knob (id::highCut).setValue (3000.0, juce::sendNotificationSync);
    REQUIRE (manager.saveUserPreset (presetName));
    CHECK (manager.userPresetExists (presetName));
    CHECK (manager.getCurrentName() == presetName);

    // It shows up after the factory presets...
    auto menu = gui.presetBar().buildPresetMenu();
    int userItem = 0;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (it.getItem().text == presetName)
            userItem = it.getItem().itemID;
    CHECK (userItem > manager.getNumFactoryPresets());

    // ...and reloading it restores its values.
    manager.loadInit();
    CHECK_THAT (getPlain (gui.proc, id::pitch), WithinAbs (12.0, 1.0e-3));
    gui.presetBar().handlePresetMenuResult (userItem);
    gui.flush();
    CHECK_THAT (getPlain (gui.proc, id::pitch), WithinAbs (-5.0, 1.0e-3));
    CHECK_THAT (getPlain (gui.proc, id::highCut), WithinAbs (3000.0, 1.0));
    CHECK (gui.button ("presetName").getButtonText() == presetName);

    // Saving again under the same name replaces the file.
    gui.knob (id::pitch).setValue (7.0, juce::sendNotificationSync);
    REQUIRE (manager.saveUserPreset (presetName));
    manager.loadInit();
    gui.presetBar().handlePresetMenuResult (userItem);
    CHECK_THAT (getPlain (gui.proc, id::pitch), WithinAbs (7.0, 1.0e-3));

    // Illegal names are refused.
    CHECK_FALSE (manager.saveUserPreset ("   "));

    file.deleteFile();
    manager.refresh();
}

// =============================================================================================
// Visualizer, rendering, layout
// =============================================================================================
TEST_CASE ("GUI: the decay visualizer follows the audio and idles when hidden", "[gui][visualizer]")
{
    Gui gui;
    auto& vis = find<ab::ui::DecayVisualizer> (*gui.editor, "visualizer");
    CHECK_FALSE (vis.isAnimating()); // not on screen: no timer, no CPU

    auto runFrames = [&] (int frames, bool withAudio)
    {
        juce::Random rng (11);
        juce::MidiBuffer midi;
        for (int f = 0; f < frames; ++f)
        {
            juce::AudioBuffer<float> buffer (2, 800); // ~1 frame of 60 fps audio
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (ch, i, withAudio ? 0.4f * (rng.nextFloat() * 2.0f - 1.0f) : 0.0f);
            gui.proc.processBlock (buffer, midi);
            vis.advanceFrame();
        }
    };

    runFrames (5, false);
    CHECK (vis.getDisplayLevel() < 0.05f);
    runFrames (30, true);
    const auto loud = vis.getDisplayLevel();
    CHECK (loud > 0.5f);
    runFrames (240, false); // 4 s of silence: the display falls with the tail
    CHECK (vis.getDisplayLevel() < loud * 0.5f);
}

TEST_CASE ("GUI: renders at every size and in every mode (no blank or identical frames)", "[gui][render]")
{
    Gui gui;
    const auto snap = [&] { return gui.editor->createComponentSnapshot (gui.editor->getLocalBounds(), true, 1.0f); };

    for (const int width : { 645, 860, 1290, 1720 })
    {
        gui.editor->setSize (width, width * AuroraBloomEditor::kBaseHeight / AuroraBloomEditor::kBaseWidth);
        INFO ("width " << width);
        CHECK (gui.editor->getHeight() == width * 360 / 860);
        const auto img = snap();
        CHECK (img.getWidth() == width);
        CHECK (colourVariety (img) > 40);
    }

    // Size limits: 75 % .. 200 %, fixed aspect ratio.
    if (auto* c = gui.editor->getConstrainer())
    {
        CHECK (c->getMinimumWidth() == 645);
        CHECK (c->getMaximumWidth() == 1720);
        CHECK_THAT (c->getFixedAspectRatio(), WithinAbs (860.0 / 360.0, 1.0e-6));
    }

    gui.editor->setSize (860, 360);
    // Each knob's drawing reflects its value.
    for (const auto& knobId : kKnobIds)
    {
        auto& s = gui.knob (knobId);
        s.setValue (s.getMinimum(), juce::sendNotificationSync);
        const auto lo = s.createComponentSnapshot (s.getLocalBounds());
        s.setValue (s.getMaximum(), juce::sendNotificationSync);
        const auto hi = s.createComponentSnapshot (s.getLocalBounds());
        INFO ("knob " << knobId);
        CHECK (imagesDiffer (lo, hi));
    }

    // Every pitch mode / sign renders (the visualizer draws different layers for each).
    for (int mode = 0; mode < 5; ++mode)
        for (const float st : { -12.0f, 0.0f, 7.0f })
        {
            gui.selector (id::pitchMode).setSelectedItemIndex (mode, juce::sendNotificationSync);
            gui.knob (id::pitch).setValue (st, juce::sendNotificationSync);
            CHECK (colourVariety (snap()) > 40);
        }
}

TEST_CASE ("GUI: layout - every control visible, inside the window, not overlapping, text fits", "[gui][layout]")
{
    Gui gui;
    auto& canvas = find<juce::Component> (*gui.editor, "canvas");
    const auto area = canvas.getLocalBounds();

    std::vector<juce::Rectangle<int>> knobBounds;
    for (const auto& knobId : kKnobIds)
    {
        auto& knob = find<ab::ui::Knob> (*gui.editor, knobId);
        INFO ("knob " << knobId);
        CHECK (knob.isVisible());
        CHECK (area.contains (knob.getBounds()));
        CHECK (knob.getWidth() >= 80);
        for (const auto& other : knobBounds)
            CHECK_FALSE (other.intersects (knob.getBounds()));
        knobBounds.push_back (knob.getBounds());

        // The value text fits its box at the widest values.
        auto& s = gui.knob (knobId);
        juce::Label* box = nullptr;
        for (auto* child : s.getChildren())
            if (auto* l = dynamic_cast<juce::Label*> (child))
                box = l;
        REQUIRE (box != nullptr);
        for (const double v : { s.getMinimum(), s.getMaximum(), s.getDoubleClickReturnValue(),
                                s.proportionOfLengthToValue (0.333) })
        {
            const auto text = s.getTextFromValue (v);
            const auto w = juce::GlyphArrangement::getStringWidth (box->getFont(), text);
            INFO ("\"" << text << "\" " << w << " px in " << box->getWidth() << " px");
            CHECK (w < static_cast<float> (box->getWidth()));
        }
    }

    auto& bar = gui.presetBar();
    CHECK (area.contains (bar.getBounds()));
    for (const auto* buttonId : { "presetPrevious", "presetName", "presetNext", "presetSave", "slotA", "slotB", "slotCopy" })
    {
        auto& b = gui.button (buttonId);
        INFO ("button " << buttonId);
        CHECK (b.isVisible());
        CHECK (bar.getLocalBounds().contains (b.getBounds()));
        CHECK (b.getWidth() >= 24);
    }

    // The visualizer, the preset bar and the knob row do not overlap.
    auto& vis = find<juce::Component> (*gui.editor, "visualizer");
    CHECK_FALSE (vis.getBounds().intersects (bar.getBounds()));
    for (const auto& kb : knobBounds)
        CHECK_FALSE (vis.getBounds().intersects (kb));
}

TEST_CASE ("GUI: tooltips, accessibility titles and FL Studio-friendly focus", "[gui][accessibility]")
{
    Gui gui;
    for (const auto& knobId : kKnobIds)
    {
        auto& s = gui.knob (knobId);
        INFO ("knob " << knobId);
        CHECK (s.getTooltip().length() > 20);
        CHECK (s.getTitle().isNotEmpty());
        CHECK (s.getWantsKeyboardFocus());
        CHECK_FALSE (s.getMouseClickGrabsKeyboardFocus()); // a click must not steal the host's shortcuts
    }
    for (const auto& selectorId : kSelectorIds)
    {
        auto& box = gui.selector (selectorId);
        INFO ("selector " << selectorId);
        CHECK (box.getTooltip().length() > 20);
        CHECK (box.getTitle().isNotEmpty());
        CHECK_FALSE (box.getMouseClickGrabsKeyboardFocus());
    }
    for (const auto* buttonId : { "presetPrevious", "presetName", "presetNext", "presetSave", "slotA", "slotB", "slotCopy" })
    {
        auto& b = gui.button (buttonId);
        INFO ("button " << buttonId);
        CHECK (b.getTooltip().isNotEmpty());
        CHECK_FALSE (b.getMouseClickGrabsKeyboardFocus());
    }
}

TEST_CASE ("GUI: opening and closing the editor while audio runs is safe", "[gui][threads]")
{
    juce::ScopedJuceInitialiser_GUI init;
    AuroraBloomProcessor proc;
    prepareStereo (proc);

    std::atomic<bool> stop { false };
    std::atomic<bool> finiteOut { true };
    std::thread audio ([&]
    {
        juce::Random rng (2);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buffer (2, 256);
        while (! stop.load())
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    buffer.setSample (ch, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));
            proc.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    if (! std::isfinite (buffer.getSample (ch, i)))
                        finiteOut = false;
        }
    });

    for (int round = 0; round < 25; ++round)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        auto* vis = dynamic_cast<ab::ui::DecayVisualizer*> (findById (*editor, "visualizer"));
        REQUIRE (vis != nullptr);
        for (int f = 0; f < 10; ++f)
            vis->advanceFrame();
        // Twist a knob from the UI while the audio thread reads it.
        auto* s = dynamic_cast<ab::ui::KnobSlider*> (findById (*editor, juce::String (id::size) + "_slider"));
        REQUIRE (s != nullptr);
        s->setValue (s->proportionOfLengthToValue ((round % 5) / 4.0), juce::sendNotificationSync);
        (void) editor->createComponentSnapshot (editor->getLocalBounds());
    }

    stop = true;
    audio.join();
    CHECK (finiteOut.load());
}
