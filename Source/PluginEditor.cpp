#include "PluginEditor.h"
#include "PluginProcessor.h"

DrumMapperAudioProcessorEditor::DrumMapperAudioProcessorEditor (DrumMapperAudioProcessor& p)
    : juce::AudioProcessorEditor (&p)
    , processor (p)
    , tableComponent (processor)
{
    titleLabel.setText ("Bitwig Drum Mapper", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (18.0f).withStyle ("Bold")));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    addAndMakeVisible (tableComponent);
    tableComponent.refresh();

    processor.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (400, 250, 1600, 1000);

    setSize (660, 440);
}

DrumMapperAudioProcessorEditor::~DrumMapperAudioProcessorEditor()
{
    stopTimer();
    processor.removeChangeListener (this);
}

void DrumMapperAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2b2b2b));
}

void DrumMapperAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    titleLabel.setBounds (area.removeFromTop (28));
    area.removeFromTop (6);

    tableComponent.setBounds (area);
}

void DrumMapperAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    tableComponent.refresh();
    repaint();
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
