#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "WebViewBridge.h"

class DrumMapperAudioProcessor;

/**
    The plugin editor: a single WebView component filling the window.

    All widgets (mapping table, note/channel editors, import/export) live in
    the embedded web page (Source/WebUI); this class only hosts the view and
    forwards host-driven change messages into the page (preset loads from the
    host), plus the CLAP-wrapper DPI workaround from the previous native UI.

    The window is resizable.
*/
class DrumMapperAudioProcessorEditor
    : public juce::AudioProcessorEditor
    , public juce::ChangeListener
    , private juce::Timer
{
public:
    explicit DrumMapperAudioProcessorEditor (DrumMapperAudioProcessor&);
    ~DrumMapperAudioProcessorEditor() override;

    void resized() override;
    void parentHierarchyChanged() override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

private:
    void timerCallback() override;

    DrumMapperAudioProcessor& processor;
    WebViewUIBridge bridge;

    int resizeCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumMapperAudioProcessorEditor)
};
