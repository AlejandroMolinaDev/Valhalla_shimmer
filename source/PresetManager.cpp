#include "PresetManager.h"

#include "BinaryData.h"
#include "Parameters.h"

namespace
{
    const juce::Identifier kPresetTag { "AuroraBloomPreset" };
    constexpr const char* kPresetExtension = ".xml";
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    refresh();
    for (const auto& p : presets)
        if (p.isFactory)
            factoryNames.add (p.name);
}

juce::File PresetManager::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Aurora Bloom")
        .getChildFile ("Presets");
}

void PresetManager::refresh()
{
    presets.clear();

    // Factory presets: every embedded *.xml resource, sorted by original filename (NN_Name.xml).
    std::vector<std::pair<juce::String, juce::String>> factory;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const auto* resource = BinaryData::namedResourceList[i];
        const juce::String original (BinaryData::getNamedResourceOriginalFilename (resource));
        if (! original.endsWithIgnoreCase (kPresetExtension))
            continue;

        int size = 0;
        if (const auto* data = BinaryData::getNamedResource (resource, size))
            factory.emplace_back (original, juce::String::fromUTF8 (data, size));
    }
    std::sort (factory.begin(), factory.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [filename, xmlText] : factory)
        if (const auto xml = juce::parseXML (xmlText); xml != nullptr && xml->hasTagName (kPresetTag))
            presets.push_back ({ xml->getStringAttribute ("name", filename), xml->getStringAttribute ("category", "Factory"),
                                 true, xmlText, {} });

    // User presets.
    const auto folder = getUserPresetFolder();
    if (folder.isDirectory())
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kPresetExtension);
        files.sort();
        for (const auto& file : files)
            if (const auto xml = juce::parseXML (file); xml != nullptr && xml->hasTagName (kPresetTag))
                presets.push_back ({ xml->getStringAttribute ("name", file.getFileNameWithoutExtension()), "User", false,
                                     {}, file });
    }

    sendChangeMessage();
}

bool PresetManager::loadPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, static_cast<int> (presets.size())))
        return false;

    const auto& preset = presets[static_cast<size_t> (index)];
    const auto xml = preset.isFactory ? juce::parseXML (preset.factoryXml) : juce::parseXML (preset.file);
    if (xml == nullptr || ! applyPresetXml (apvts, *xml))
        return false;

    setCurrentName (preset.name);
    currentFactory = preset.isFactory ? index : -1;
    modified = false;
    sendChangeMessage();
    return true;
}

void PresetManager::loadInit()
{
    for (const auto& id : ab::params::allIds())
        if (auto* param = apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->getDefaultValue());
            param->endChangeGesture();
        }

    setCurrentName ("Init");
    currentFactory = -1;
    modified = false;
    sendChangeMessage();
}

int PresetManager::getCurrentIndex() const
{
    const auto name = getCurrentName();
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name == name)
            return static_cast<int> (i);
    return -1;
}

void PresetManager::loadNext()
{
    if (! presets.empty())
        loadPreset ((getCurrentIndex() + 1) % static_cast<int> (presets.size()));
}

void PresetManager::loadPrevious()
{
    const auto current = getCurrentIndex();
    if (! presets.empty())
        loadPreset (current <= 0 ? static_cast<int> (presets.size()) - 1 : current - 1);
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return false;

    const auto folder = getUserPresetFolder();
    if (! folder.createDirectory())
        return false;

    const auto xml = createPresetXml (apvts, rawName.trim(), "User");
    const auto file = folder.getChildFile (name + kPresetExtension);
    if (! xml->writeTo (file))
        return false;

    setCurrentName (rawName.trim());
    modified = false;
    refresh();
    return true;
}

bool PresetManager::userPresetExists (const juce::String& rawName) const
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    return name.isNotEmpty() && getUserPresetFolder().getChildFile (name + kPresetExtension).existsAsFile();
}

juce::String PresetManager::getCurrentName() const
{
    const juce::ScopedLock sl (nameLock);
    return currentName;
}

void PresetManager::restoreCurrentName (const juce::String& name)
{
    setCurrentName (name);
    currentFactory = factoryNames.indexOf (name); // immutable list: safe from any thread
    modified = false;
}

void PresetManager::setCurrentName (const juce::String& name)
{
    {
        const juce::ScopedLock sl (nameLock);
        currentName = name.isNotEmpty() ? name : juce::String ("Init");
    }
    sendChangeMessage();
}

// =============================================================================================
void PresetManager::toggleSlot()
{
    slots[static_cast<size_t> (activeSlot)] = apvts.copyState();
    activeSlot ^= 1;

    auto& other = slots[static_cast<size_t> (activeSlot)];
    if (other.isValid())
        apvts.replaceState (other.createCopy());
    else
        other = apvts.copyState(); // first visit: B starts as a copy of A

    sendChangeMessage();
}

void PresetManager::copyToOtherSlot()
{
    slots[static_cast<size_t> (activeSlot ^ 1)] = apvts.copyState();
    sendChangeMessage();
}

// =============================================================================================
std::unique_ptr<juce::XmlElement> PresetManager::createPresetXml (const juce::AudioProcessorValueTreeState& state,
                                                                  const juce::String& name, const juce::String& category)
{
    auto xml = std::make_unique<juce::XmlElement> (kPresetTag);
    xml->setAttribute ("name", name);
    xml->setAttribute ("category", category);
    xml->setAttribute ("version", ab::params::kParameterVersion);

    for (const auto& id : ab::params::allIds())
        if (auto* param = state.getParameter (id))
        {
            auto* child = xml->createNewChildElement ("PARAM");
            child->setAttribute ("id", id);
            child->setAttribute ("value", static_cast<double> (param->convertFrom0to1 (param->getValue())));
        }

    return xml;
}

bool PresetManager::applyPresetXml (juce::AudioProcessorValueTreeState& state, const juce::XmlElement& xml)
{
    if (! xml.hasTagName (kPresetTag))
        return false;

    // Parameters missing from the preset fall back to their defaults.
    for (const auto& id : ab::params::allIds())
    {
        auto* param = state.getParameter (id);
        if (param == nullptr)
            continue;

        float normalised = param->getDefaultValue();
        for (auto* child : xml.getChildWithTagNameIterator ("PARAM"))
            if (child->getStringAttribute ("id") == id)
                normalised = param->convertTo0to1 (static_cast<float> (child->getDoubleAttribute ("value")));

        param->beginChangeGesture();
        param->setValueNotifyingHost (normalised);
        param->endChangeGesture();
    }
    return true;
}
