#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "MappingTableComponent.h"

class DrumMapperAudioProcessor;

/**
    The plugin editor. Hosts the mapping table and listens to the processor so
    that a host-driven state change (preset load) refreshes the table.

    The window is resizable. High-DPI scaling is handled in the CLAP wrapper
    (guiWin32Attach) so the editor always works in physical pixels.
*/
class DrumMapperAudioProcessorEditor
    : public juce::AudioProcessorEditor
    , public juce::ChangeListener
    , private juce::Timer
{
public:
    explicit DrumMapperAudioProcessorEditor (DrumMapperAudioProcessor&);
    ~DrumMapperAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    void timerCallback() override;

    DrumMapperAudioProcessor& processor;

    juce::Label titleLabel;
    MappingTableComponent tableComponent;

    int resizeCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumMapperAudioProcessorEditor)
};
