#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wunused-parameter")
#include <clap-juce-extensions/clap-juce-extensions.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#include "NoteMapping.h"

/**
    Main audio processor for the Bitwig Drum Mapper.

    This is a pure MIDI effect: it has no audio buses. It reads note on/off
    events from the incoming MIDI buffer, remaps their note number and channel
    according to the user-edited NoteMapping, and writes the result to the
    outgoing MIDI buffer.

    The processor is also a ChangeBroadcaster: it notifies the editor whenever
    the mapping changes structurally (rows added/removed/cleared, or a preset
    is loaded by the host) so the table view can refresh.

    It implements the CLAP note-name extension so the host (Bitwig) can display
    the mapping names on its piano roll / note lanes.
*/
class DrumMapperAudioProcessor
    : public juce::AudioProcessor
    , public juce::ChangeBroadcaster
    , public clap_juce_extensions::clap_juce_audio_processor_capabilities
{
public:
    DrumMapperAudioProcessor();
    ~DrumMapperAudioProcessor() override = default;

    // ---- Mapping access (call from the message thread / editor) --------------
    int  getNumEntries() const;
    MappingEntry getEntry (int index) const;

    /** Edit operations. Each one locks the mapping, applies the change, tells
        the host the non-parameter state changed, and broadcasts to listeners. */
    void setEntry (int index, const MappingEntry& entry);
    void addEntry();
    void insertEntry (int index);
    void removeEntry (int index);
    void clearEntries();

    /** Export the current mapping to a .bwdrm file. Returns true on success. */
    bool exportToFile (const juce::File& file);

    /** Import a .bwdrm file, replacing the current mapping. Returns true on success. */
    bool importFromFile (const juce::File& file);

    /** Audio-thread safe remap. Uses a try-lock; if the mapping is being edited
        this block simply passes notes through unchanged. */
    bool remapNote (int inNote, int inChannel, int& outNote, int& outChannel) const noexcept;

    // ---- AudioProcessor overrides -------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }

    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- CLAP note-name extension -------------------------------------------
    /** Tells the host we provide custom note names. */
    bool supportsNoteName() const noexcept override { return true; }

    /** Returns the number of note names (one per mapping entry). */
    uint32_t noteNameCount() noexcept override;

    /** Fills in the note name for the given index. */
    bool noteNameGet (uint32_t index, clap_note_name* noteName) noexcept override;

private:
    /** Lock guarding `mapping`. Held by the editor on write and try-locked by
        the audio thread on read. */
    mutable juce::SpinLock mappingLock;
    NoteMapping mapping;

    void markStateChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumMapperAudioProcessor)
};
