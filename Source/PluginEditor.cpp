#include "PluginEditor.h"
#include "PluginProcessor.h"

DrumMapperAudioProcessorEditor::DrumMapperAudioProcessorEditor (DrumMapperAudioProcessor& p)
    : juce::AudioProcessorEditor (&p)
    , processor (p)
    , bridge (p)
{
    addAndMakeVisible (bridge.getWebComponent());
    bridge.getWebComponent().goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    processor.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (480, 320, 1600, 1000);

    setSize (720, 500);
}

DrumMapperAudioProcessorEditor::~DrumMapperAudioProcessorEditor()
{
    stopTimer();
    processor.removeChangeListener (this);
}

juce::AudioProcessorEditor* DrumMapperAudioProcessor::createEditor()
{
    return new DrumMapperAudioProcessorEditor (*this);
}

void DrumMapperAudioProcessorEditor::resized()
{
    bridge.getWebComponent().setBounds (getLocalBounds());
}

void DrumMapperAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A structural change (row added/removed/cleared, import, host preset
    // load) must be mirrored into the page.
    bridge.pushState();
}

void DrumMapperAudioProcessorEditor::parentHierarchyChanged()
{
    // When the CLAP wrapper attaches our window (addToDesktop), the peer is
    // created with the system DPI scale (e.g. 1.5 at 150%).  The wrapper then
    // overrides the scale to 1.0, but the host window size was already set
    // using the old scale.  We trigger a delayed resize to force the host to
    // re-query our size, which is now correct at scale 1.0.
    startTimerHz (20);
}

void DrumMapperAudioProcessorEditor::timerCallback()
{
    // Re-assert our size a few times so the host window catches up with the
    // corrected scale factor.
    setSize (getWidth(), getHeight());
    ++resizeCounter;

    if (resizeCounter >= 5)
        stopTimer();
}
