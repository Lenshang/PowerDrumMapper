/*
    Headless test runner for PowerDrumMapper (formerly BitwigDrumMapper).

    Exercises the plugin's core behaviour without a host or audio device:
      - note remapping (source note/channel -> target note/channel);
      - host note-name queries (VST3 IUnitInfo pitch names / VST2
        effGetMidiKeyName path) and CLAP note-name reporting;
      - state (entries) save / load round-trip;
      - CSV import / export;
      - free-text note parsing (used by the autocomplete note cells).

    Returns 0 when every check passes, 1 otherwise.
*/

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/NoteMapping.h"
#include "../Source/NoteNameUtils.h"

namespace
{
    int failures = 0;
    int checks   = 0;

    void check (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok)
        {
            ++failures;
            std::printf ("FAIL: %s\n", what.toRawUTF8());
        }
    }

    //==========================================================================
    void testRemapBasics()
    {
        std::printf ("  testRemapBasics\n");
        DrumMapperAudioProcessor p;

        // Default map: Kick 24(ALL)->36(ch10), Snare 26->38(ch10), ESnare 27->39(ch10).
        int outNote = -1, outChan = -1;
        check (p.remapNote (24, 5, outNote, outChan) && outNote == 36 && outChan == 10,
               "remap: Kick source 24 -> target 36 ch10");
        check (p.remapNote (26, 1, outNote, outChan) && outNote == 38 && outChan == 10,
               "remap: Snare source 26 -> target 38 ch10");
        check (p.remapNote (27, 16, outNote, outChan) && outNote == 39 && outChan == 10,
               "remap: ESnare source 27 -> target 39 ch10");
        check (! p.remapNote (60, 1, outNote, outChan), "remap: unlisted note passes through");

        // Channel filtering: add an entry with a specific source channel.
        p.addEntry();
        const int newRow = p.getNumEntries() - 1;
        p.setEntry (newRow, { "Test", 50, 4, 70, 0 });   // source ch 4 only, target ALL
        check (p.remapNote (50, 4, outNote, outChan) && outNote == 70 && outChan == 4,
               "remap: channel filter matches configured channel");
        check (! p.remapNote (50, 5, outNote, outChan), "remap: channel filter rejects other channel");
    }

    void testParseNoteText()
    {
        std::printf ("  testParseNoteText\n");

        // Item label with parenthesised number.
        check (NoteNameUtils::parseNoteText ("C1 (36)") == 36, "parse: full item label \"C1 (36)\"");
        check (NoteNameUtils::parseNoteText ("D#1 (39)") == 39, "parse: full item label \"D#1 (39)\"");

        // Bare note names (DAW convention, C3 = 60).
        check (NoteNameUtils::parseNoteText ("C1") == 36, "parse: name \"C1\"");
        check (NoteNameUtils::parseNoteText ("D#2") == 51, "parse: sharp name \"D#2\"");
        check (NoteNameUtils::parseNoteText ("Bb0") == 34, "parse: flat name \"Bb0\"");
        check (NoteNameUtils::parseNoteText ("  C1  ") == 36, "parse: surrounding whitespace trimmed");

        // Raw numbers.
        check (NoteNameUtils::parseNoteText ("36") == 36, "parse: raw number");
        check (NoteNameUtils::parseNoteText ("127") == 127, "parse: max note");
        check (NoteNameUtils::parseNoteText ("0") == 0, "parse: zero note");

        // Rejections.
        check (NoteNameUtils::parseNoteText ("128") == -1, "parse: 128 rejected");
        check (NoteNameUtils::parseNoteText ("-3") == -1, "parse: negative rejected");
        check (NoteNameUtils::parseNoteText ("C1 (3") == -1, "parse: half-typed label rejected");
        check (NoteNameUtils::parseNoteText ("C1 (abc)") == -1, "parse: non-numeric parens rejected");
        check (NoteNameUtils::parseNoteText ("hello") == -1, "parse: garbage rejected");
        check (NoteNameUtils::parseNoteText ("") == -1, "parse: empty rejected");
    }

    void testHostNoteNameQueries()
    {
        std::printf ("  testHostNoteNameQueries\n");
        DrumMapperAudioProcessor p;

        // The VST3 (IUnitInfo pitch names) and VST2 (effGetMidiKeyName) paths
        // both go through AudioProcessor::getNameForMidiNoteNumber, matched on
        // the SOURCE note.
        const auto kick = p.getNameForMidiNoteNumber (24, 1);
        check (kick.has_value() && *kick == "Kick", "host query: source note 24 is \"Kick\"");
        check (p.getNameForMidiNoteNumber (26, 10).has_value() && *p.getNameForMidiNoteNumber (26, 10) == "Snare",
               "host query: names apply to every channel");

        check (! p.getNameForMidiNoteNumber (36, 1).has_value(), "host query: target note (36) has no name");
        check (! p.getNameForMidiNoteNumber (60, 1).has_value(), "host query: unlisted note has no name");
        check (! p.getNameForMidiNoteNumber (-1, 1).has_value(), "host query: negative note safe");
        check (! p.getNameForMidiNoteNumber (200, 1).has_value(), "host query: note > 127 safe (some hosts ask)");

        p.clearEntries();
        check (! p.getNameForMidiNoteNumber (24, 1).has_value(), "host query: no names after clear");
    }

    void testClapNoteNameExtension()
    {
        std::printf ("  testClapNoteNameExtension\n");
        DrumMapperAudioProcessor p;

        check (p.supportsNoteName(), "clap: note-name extension supported");
        check (p.noteNameCount() == 3, "clap: 3 default entries reported");

        clap_note_name name {};
        check (p.noteNameGet (0, &name), "clap: index 0 available");
        check (juce::String (name.name) == "Kick", "clap: name 0 is \"Kick\"");
        check (name.key == 24, "clap: key 0 is 24 (source note)");
        check (name.channel == -1, "clap: channel -1 (every channel)");

        check (! p.noteNameGet (3, &name), "clap: out-of-range index rejected");
    }

    void testStateRoundTrip()
    {
        std::printf ("  testStateRoundTrip\n");
        DrumMapperAudioProcessor p;

        p.clearEntries();
        p.addEntry();
        p.setEntry (0, { "Kick", 30, 0, 40, 10 });
        p.addEntry();
        p.setEntry (1, { "Ride", 51, 0, 51, 0 });

        juce::MemoryBlock state;
        p.getStateInformation (state);

        DrumMapperAudioProcessor q;   // fresh instance, defaults
        q.setStateInformation (state.getData(), (int) state.getSize());

        check (q.getNumEntries() == 2, "state: 2 entries restored");
        check (q.getEntry (0).name == "Kick" && q.getEntry (0).sourceNote == 30
               && q.getEntry (0).targetNote == 40 && q.getEntry (0).targetChannel == 10,
               "state: entry 0 fully restored");
        check (q.getEntry (1).name == "Ride" && q.getEntry (1).sourceNote == 51,
               "state: entry 1 restored");

        // The restored entry really drives the remap.
        int outNote = -1, outChan = -1;
        check (q.remapNote (30, 2, outNote, outChan) && outNote == 40 && outChan == 10,
               "state: restored mapping is active");

        // And invalid state is rejected without touching the mapping.
        const char junk[] = "this is not valid state data";
        q.setStateInformation (junk, (int) sizeof (junk));
        check (q.getNumEntries() == 2, "state: invalid state leaves mapping unchanged");
    }

    void testCsvRoundTrip()
    {
        std::printf ("  testCsvRoundTrip\n");

        // Format: name,sourceNote,sourceChannel,targetNote,targetChannel
        NoteMapping m = NoteMapping::createDefaultDrumMap();
        const auto csv = m.toCsvString();
        check (csv == "Kick,24,0,36,10\nSnare,26,0,38,10\nESnare,27,0,39,10",
               "csv: exact default export");

        NoteMapping loaded;
        check (loaded.fromCsvString (csv), "csv: import succeeds");
        check (loaded.getNumEntries() == 3
               && loaded.getEntry (0).name == "Kick"
               && loaded.getEntry (2).sourceNote == 27
               && loaded.getEntry (2).targetNote == 39
               && loaded.getEntry (2).targetChannel == 10,
               "csv: round-trip preserves all fields");

        check (! loaded.fromCsvString ("garbage,line,only"), "csv: malformed input rejected");
        check (loaded.getNumEntries() == 3, "csv: failed import leaves mapping untouched");
    }
}

//==============================================================================
int main()
{
    // Keep test output flowing even when something dies mid-run.
    std::setvbuf (stdout, nullptr, _IONBF, 0);

    // AudioProcessor / ChangeBroadcaster rely on the JUCE message machinery.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    std::printf ("=== PowerDrumMapper tests ===\n");

    testRemapBasics();
    testParseNoteText();
    testHostNoteNameQueries();
    testClapNoteNameExtension();
    testStateRoundTrip();
    testCsvRoundTrip();

    std::printf ("=== %d checks, %d failures ===\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
