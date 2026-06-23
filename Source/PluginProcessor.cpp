#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NoteNameUtils.h"

DrumMapperAudioProcessor::DrumMapperAudioProcessor()
    : juce::AudioProcessor (BusesProperties())
{
    mapping = NoteMapping::createDefaultDrumMap();
}

//==============================================================================
void DrumMapperAudioProcessor::prepareToPlay (double, int)
{
    // Nothing to prepare for a pure MIDI effect.
}

bool DrumMapperAudioProcessor::isBusesLayoutSupported (const BusesLayout&) const
{
    // A MIDI effect has no audio buses; accept whatever layout the host probes.
    return true;
}

void DrumMapperAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    juce::MidiBuffer output;

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        const auto samplePos = metadata.samplePosition;

        if (message.isNoteOnOrOff())
        {
            int outNote = 0;
            int outChannel = 0;

            if (remapNote (message.getNoteNumber(), message.getChannel(), outNote, outChannel))
            {
                // Create fresh messages instead of mutating copies — avoids
                // any subtle raw-byte issues across wrapper boundaries.
                const auto velocity = message.getVelocity();

                output.addEvent (message.isNoteOn()
                                     ? juce::MidiMessage::noteOn  (outChannel, outNote, velocity)
                                     : juce::MidiMessage::noteOff (outChannel, outNote, velocity),
                                 samplePos);
                continue;
            }
        }

        // Pass through unmodified (including non-note events).
        output.addEvent (message, samplePos);
    }

    midiMessages.swapWith (output);
}

//==============================================================================
int DrumMapperAudioProcessor::getNumEntries() const
{
    const juce::SpinLock::ScopedLockType sl (mappingLock);
    return mapping.getNumEntries();
}

MappingEntry DrumMapperAudioProcessor::getEntry (int index) const
{
    const juce::SpinLock::ScopedLockType sl (mappingLock);
    return mapping.getEntry (index);
}

void DrumMapperAudioProcessor::setEntry (int index, const MappingEntry& entry)
{
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        mapping.setEntry (index, entry);
    }
    // Value edits don't change the table structure, so just mark host state dirty.
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails{}.withNonParameterStateChanged (true));
    // Note names may have changed (name or source note/channel).
    noteNamesChanged();
}

void DrumMapperAudioProcessor::addEntry()
{
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        mapping.addEntry ({ "New", 60, 0, 60, 0 });
    }
    markStateChanged();
}

void DrumMapperAudioProcessor::insertEntry (int index)
{
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        mapping.insertEntry (index, { "New", 60, 0, 60, 0 });
    }
    markStateChanged();
}

void DrumMapperAudioProcessor::removeEntry (int index)
{
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        mapping.removeEntry (index);
    }
    markStateChanged();
}

void DrumMapperAudioProcessor::clearEntries()
{
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        mapping.clear();
    }
    markStateChanged();
}

//==============================================================================
bool DrumMapperAudioProcessor::exportToFile (const juce::File& file)
{
    juce::String csv;
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        csv = mapping.toCsvString();
    }

    juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (csv))
        return false;

    return temp.overwriteTargetFileWithTemporary();
}

bool DrumMapperAudioProcessor::importFromFile (const juce::File& file)
{
    auto text = file.loadFileAsString();
    bool changed = false;

    const bool isXml = text.trimStart().startsWithChar ('<')
                       || file.hasFileExtension ("drm");

    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        changed = isXml ? mapping.fromCubaseDrm (text)
                        : mapping.fromCsvString (text);
    }

    if (changed)
        markStateChanged();

    return changed;
}

bool DrumMapperAudioProcessor::remapNote (int inNote, int inChannel, int& outNote, int& outChannel) const noexcept
{
    // Never block the audio thread: if the message thread is editing, just pass through.
    const juce::SpinLock::ScopedTryLockType tryLock (mappingLock);
    if (! tryLock.isLocked())
        return false;

    return mapping.remap (inNote, inChannel, outNote, outChannel);
}

void DrumMapperAudioProcessor::markStateChanged()
{
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails{}.withNonParameterStateChanged (true));
    sendChangeMessage();
    // Structural change means note names changed too.
    noteNamesChanged();
}

//==============================================================================
void DrumMapperAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree tree;
    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        tree = mapping.toValueTree();
    }

    if (auto xml = tree.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, destData);
}

void DrumMapperAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (data, sizeInBytes))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);
        bool changed = false;
        {
            const juce::SpinLock::ScopedLockType sl (mappingLock);
            changed = mapping.fromValueTree (tree);
        }

        if (changed)
            markStateChanged();
    }
}

//==============================================================================
juce::AudioProcessorEditor* DrumMapperAudioProcessor::createEditor()
{
    return new DrumMapperAudioProcessorEditor (*this);
}

//==============================================================================
// CLAP note-name extension
//==============================================================================
uint32_t DrumMapperAudioProcessor::noteNameCount() noexcept
{
    const juce::SpinLock::ScopedLockType sl (mappingLock);
    return static_cast<uint32_t> (mapping.getNumEntries());
}

bool DrumMapperAudioProcessor::noteNameGet (uint32_t index, clap_note_name* noteName) noexcept
{
    const juce::SpinLock::ScopedLockType sl (mappingLock);

    if (index >= static_cast<uint32_t> (mapping.getNumEntries()))
        return false;

    const auto& entry = mapping.getEntry (static_cast<int> (index));

    // Copy the mapping name into the CLAP struct.
    entry.name.copyToUTF8 (noteName->name, CLAP_NAME_SIZE);

    // Report the SOURCE note — this is what the user plays to trigger the drum.
    noteName->key = static_cast<int16_t> (entry.sourceNote);

    // CLAP uses channels 0-15; our convention is 0 = ALL.
    // For ALL, report -1 (every channel).
    noteName->channel = NoteNameUtils::isAllChannels (entry.sourceChannel)
                            ? -1
                            : static_cast<int16_t> (entry.sourceChannel - 1);

    noteName->port = -1;  // every port

    return true;
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DrumMapperAudioProcessor();
}
