#include "NoteMapping.h"
#include "NoteNameUtils.h"

MappingEntry NoteMapping::getEntry (int index) const
{
    if (index >= 0 && index < (int) entries.size())
        return entries[(size_t) index];

    return {};
}

void NoteMapping::setEntry (int index, const MappingEntry& entry)
{
    if (index >= 0 && index < (int) entries.size())
        entries[(size_t) index] = entry;
}

void NoteMapping::addEntry (const MappingEntry& entry)
{
    entries.push_back (entry);
}

void NoteMapping::insertEntry (int index, const MappingEntry& entry)
{
    if (index < 0)
        index = 0;
    if (index > (int) entries.size())
        index = (int) entries.size();

    entries.insert (entries.begin() + index, entry);
}

void NoteMapping::removeEntry (int index)
{
    if (index >= 0 && index < (int) entries.size())
        entries.erase (entries.begin() + index);
}

void NoteMapping::clear()
{
    entries.clear();
}

bool NoteMapping::remap (int inNote, int inChannel, int& outNote, int& outChannel) const noexcept
{
    for (const auto& e : entries)
    {
        const bool channelMatch = NoteNameUtils::isAllChannels (e.sourceChannel)
                                  || e.sourceChannel == inChannel;

        if (channelMatch && e.sourceNote == inNote)
        {
            outNote = e.targetNote;
            outChannel = NoteNameUtils::isAllChannels (e.targetChannel) ? inChannel : e.targetChannel;
            return true;
        }
    }

    return false;
}

juce::ValueTree NoteMapping::toValueTree() const
{
    juce::ValueTree root ("Mapping");
    root.setProperty ("version", 1, nullptr);

    for (const auto& e : entries)
    {
        juce::ValueTree child ("Entry");
        child.setProperty ("name", e.name, nullptr);
        child.setProperty ("sourceNote", e.sourceNote, nullptr);
        child.setProperty ("sourceChannel", e.sourceChannel, nullptr);
        child.setProperty ("targetNote", e.targetNote, nullptr);
        child.setProperty ("targetChannel", e.targetChannel, nullptr);
        root.appendChild (child, nullptr);
    }

    return root;
}

bool NoteMapping::fromValueTree (const juce::ValueTree& tree)
{
    if (! tree.isValid())
        return false;

    std::vector<MappingEntry> loaded;

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto child = tree.getChild (i);
        if (child.getType() != juce::Identifier ("Entry"))
            continue;

        MappingEntry e;
        e.name = child.getProperty ("name", juce::String()).toString();
        e.sourceNote = (int) child.getProperty ("sourceNote", 60);
        e.sourceChannel = (int) child.getProperty ("sourceChannel", 0);
        e.targetNote = (int) child.getProperty ("targetNote", 60);
        e.targetChannel = (int) child.getProperty ("targetChannel", 0);

        // Clamp to sane ranges.
        e.sourceNote = juce::jlimit (0, 127, e.sourceNote);
        e.targetNote = juce::jlimit (0, 127, e.targetNote);
        e.sourceChannel = juce::jlimit (0, 16, e.sourceChannel);
        e.targetChannel = juce::jlimit (0, 16, e.targetChannel);

        loaded.push_back (e);
    }

    entries = std::move (loaded);
    return true;
}

NoteMapping NoteMapping::createDefaultDrumMap()
{
    // Matches the example from the project brief.
    // C0=24, C1=36, D0=26, D1=38, D#0=27, D#1=39 (Bitwig convention: C3=60)
    NoteMapping m;
    m.addEntry ({ "Kick",   24, 0, 36, 10 }); // C0 -> C1, ch 10
    m.addEntry ({ "Snare",  26, 0, 38, 10 }); // D0 -> D1, ch 10
    m.addEntry ({ "ESnare", 27, 0, 39, 10 }); // D#0 -> D#1, ch 10
    return m;
}

//==============================================================================
juce::String NoteMapping::toCsvString() const
{
    juce::StringArray lines;

    for (const auto& e : entries)
    {
        // Format: name,sourcenote,sourcechannel,targetnote,targetchannel
        lines.add (e.name + ","
                       + juce::String (e.sourceNote) + ","
                       + juce::String (e.sourceChannel) + ","
                       + juce::String (e.targetNote) + ","
                       + juce::String (e.targetChannel));
    }

    return lines.joinIntoString ("\n");
}

bool NoteMapping::fromCsvString (const juce::String& text)
{
    std::vector<MappingEntry> loaded;
    auto lines = juce::StringArray::fromLines (text);

    for (const auto& line : lines)
    {
        auto trimmed = line.trim();
        if (trimmed.isEmpty())
            continue;

        auto tokens = juce::StringArray::fromTokens (trimmed, ",", "");
        tokens.removeEmptyStrings (false);

        if (tokens.size() < 5)
            continue;  // skip malformed lines

        MappingEntry e;
        e.name = tokens[0];
        e.sourceNote     = juce::jlimit (0, 127, tokens[1].getIntValue());
        e.sourceChannel  = juce::jlimit (0, 16, tokens[2].getIntValue());
        e.targetNote     = juce::jlimit (0, 127, tokens[3].getIntValue());
        e.targetChannel  = juce::jlimit (0, 16, tokens[4].getIntValue());

        loaded.push_back (e);
    }

    if (loaded.empty())
        return false;

    entries = std::move (loaded);
    return true;
}

//==============================================================================
bool NoteMapping::fromCubaseDrm (const juce::String& xmlText)
{
    auto doc = juce::XmlDocument::parse (xmlText);
    if (doc == nullptr || ! doc->hasTagName ("DrumMap"))
        return false;

    std::vector<MappingEntry> loaded;

    // Iterate through <list name="Map"> → <item> elements
    for (auto* listElem = doc->getFirstChildElement(); listElem != nullptr;
         listElem = listElem->getNextElement())
    {
        if (! listElem->hasTagName ("list") || listElem->getStringAttribute ("name") != "Map")
            continue;

        for (auto* item = listElem->getFirstChildElement(); item != nullptr;
             item = item->getNextElement())
        {
            if (! item->hasTagName ("item"))
                continue;

            MappingEntry e;

            for (auto* prop = item->getFirstChildElement(); prop != nullptr;
                 prop = prop->getNextElement())
            {
                const auto attrName = prop->getStringAttribute ("name");

                if (attrName == "INote")
                    e.sourceNote = juce::jlimit (0, 127, prop->getIntAttribute ("value"));
                else if (attrName == "ONote")
                    e.targetNote = juce::jlimit (0, 127, prop->getIntAttribute ("value"));
                else if (attrName == "Channel")
                {
                    const int ch = prop->getIntAttribute ("value");
                    // Cubase: -1 = unchanged/ALL, 0-15 = MIDI channels 1-16
                    e.targetChannel = (ch < 0) ? 0 : juce::jlimit (0, 16, ch + 1);
                }
                else if (attrName == "Name")
                    e.name = prop->getStringAttribute ("value");
            }

            // Source channel is always ALL — Cubase drum maps don't filter input.
            e.sourceChannel = 0;

            loaded.push_back (e);
        }
    }

    if (loaded.empty())
        return false;

    entries = std::move (loaded);
    return true;
}
