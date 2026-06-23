#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

/**
    A single source->target note/channel mapping rule.

    Channels use the project convention: 0 means "ALL".
      - sourceChannel == ALL matches any incoming channel.
      - targetChannel == ALL preserves the original incoming channel.
*/
struct MappingEntry
{
    juce::String name;
    int sourceNote = 60;       // 0-127
    int sourceChannel = 0;     // 0 = ALL (any channel)
    int targetNote = 60;       // 0-127
    int targetChannel = 0;     // 0 = ALL (keep original channel)
};

/**
    Owns the ordered list of mapping entries and performs the actual
    note remapping. This is a plain data model (no UI / threading concerns):
    mutation happens on the message thread from the editor, and remap() is
    called from the audio thread in processBlock().
*/
class NoteMapping
{
public:
    NoteMapping() = default;

    int  getNumEntries() const noexcept { return (int) entries.size(); }
    bool isEmpty()       const noexcept { return entries.empty(); }

    MappingEntry getEntry (int index) const;
    void setEntry (int index, const MappingEntry& entry);
    void addEntry (const MappingEntry& entry);
    void insertEntry (int index, const MappingEntry& entry);
    void removeEntry (int index);
    void clear();

    /** Returns the raw entries (read only). Used by the table model. */
    const std::vector<MappingEntry>& getEntries() const noexcept { return entries; }

    /**
        Remaps an incoming note/channel.

        @param inNote     Incoming MIDI note number (0-127).
        @param inChannel  Incoming MIDI channel (1-16).
        @param outNote    Filled with the target note number on a match.
        @param outChannel Filled with the target channel (1-16) on a match.
        @returns true if a matching rule was found.
    */
    bool remap (int inNote, int inChannel, int& outNote, int& outChannel) const noexcept;

    /** Serialises the mapping to a ValueTree for host state / preset storage. */
    juce::ValueTree toValueTree() const;

    /** Replaces all entries from a ValueTree previously produced by toValueTree(). */
    bool fromValueTree (const juce::ValueTree& tree);

    /** A small starter mapping demonstrating the drum-remap concept. */
    static NoteMapping createDefaultDrumMap();

    /** Exports the mapping to a CSV string (one entry per line). */
    juce::String toCsvString() const;

    /** Imports a mapping from a CSV string, replacing all entries.
        Returns true on success. Invalid lines are silently skipped. */
    bool fromCsvString (const juce::String& text);

    /** Imports a Cubase .drm drum map file (XML format). Returns true on success. */
    bool fromCubaseDrm (const juce::String& xmlText);

private:
    std::vector<MappingEntry> entries;
};
