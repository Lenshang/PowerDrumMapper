#include "MappingTableComponent.h"
#include "PluginProcessor.h"
#include "NoteNameUtils.h"

namespace
{
    //==========================================================================
    // Cell components. Each keeps a row index; when the table refreshes it
    // calls setRow() to point the component at its current row and value
    // (reusing the component instead of recreating it, to preserve focus).

    // Editable text cell for the mapping name.
    class NameCellComponent : public juce::Label
    {
    public:
        explicit NameCellComponent (DrumMapperAudioProcessor& p)
            : processor (p)
        {
            setEditable (false, true);
            setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
            setJustificationType (juce::Justification::centredLeft);
        }

        void setRow (int newRow)
        {
            row = newRow;
            juce::Label::setText (processor.getEntry (row).name, juce::dontSendNotification);
        }

        void textWasEdited() override
        {
            auto entry = processor.getEntry (row);
            entry.name = getText();
            processor.setEntry (row, entry);
        }

    private:
        DrumMapperAudioProcessor& processor;
        int row = 0;
    };

    // Combo box cell for selecting a MIDI note (0-127).
    class NoteComboCell : public juce::ComboBox
    {
    public:
        NoteComboCell (DrumMapperAudioProcessor& p, bool isSource)
            : processor (p), source (isSource)
        {
            for (int n = 0; n < 128; ++n)
                addItem (NoteNameUtils::midiToNameWithNumber (n), n + 1); // combo id = note + 1

            onChange = [this] { commit(); };
        }

        void setRow (int newRow)
        {
            row = newRow;
            const auto entry = processor.getEntry (row);
            const int note = source ? entry.sourceNote : entry.targetNote;
            setSelectedId (note + 1, juce::dontSendNotification);
        }

        void commit()
        {
            auto entry = processor.getEntry (row);
            const int note = juce::jlimit (0, 127, getSelectedId() - 1);

            if (source)
                entry.sourceNote = note;
            else
                entry.targetNote = note;

            processor.setEntry (row, entry);
        }

    private:
        DrumMapperAudioProcessor& processor;
        const bool source;
        int row = 0;
    };

    // Combo box cell for selecting a channel ("ALL" / 1..16).
    class ChannelComboCell : public juce::ComboBox
    {
    public:
        ChannelComboCell (DrumMapperAudioProcessor& p, bool isSource)
            : processor (p), source (isSource)
        {
            addItem ("ALL", 1); // combo id = channel + 1 (ALL -> 0)

            for (int c = 1; c <= 16; ++c)
                addItem (juce::String (c), c + 1);

            onChange = [this] { commit(); };
        }

        void setRow (int newRow)
        {
            row = newRow;
            const auto entry = processor.getEntry (row);
            const int channel = source ? entry.sourceChannel : entry.targetChannel;
            setSelectedId (channel + 1, juce::dontSendNotification);
        }

        void commit()
        {
            auto entry = processor.getEntry (row);
            const int channel = juce::jlimit (0, 16, getSelectedId() - 1);

            if (source)
                entry.sourceChannel = channel;
            else
                entry.targetChannel = channel;

            processor.setEntry (row, entry);
        }

    private:
        DrumMapperAudioProcessor& processor;
        const bool source;
        int row = 0;
    };
}

//==============================================================================
MappingTableComponent::MappingTableComponent (DrumMapperAudioProcessor& processorToUse)
    : processor (processorToUse)
{
    addAndMakeVisible (table);
    table.setColour (juce::ListBox::outlineColourId, juce::Colours::grey);
    table.setOutlineThickness (1);
    table.getHorizontalScrollBar().setVisible (false);   // no horizontal scroll
    // Column widths are set entirely manually in resized() to guarantee they
    // never exceed the visible viewport width (stretch-to-fit miscalculates
    // at Windows high DPI scaling like 150%).

    setupColumns();

    addAndMakeVisible (addButton);
    addAndMakeVisible (removeButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (importButton);
    addAndMakeVisible (exportButton);

    addButton.addListener (this);
    removeButton.addListener (this);
    clearButton.addListener (this);
    importButton.addListener (this);
    exportButton.addListener (this);

    setSize (600, 380);
}

MappingTableComponent::~MappingTableComponent()
{
    addButton.removeListener (this);
    removeButton.removeListener (this);
    clearButton.removeListener (this);
    importButton.removeListener (this);
    exportButton.removeListener (this);
}

void MappingTableComponent::setupColumns()
{
    auto& header = table.getHeader();

    // Proportional initial widths; stretch-to-fit will adjust them to fill
    // the table area exactly on every resize.
    header.addColumn ("Name",          nameCol,          70);
    header.addColumn ("SourceNote",    sourceNoteCol,    120);
    header.addColumn ("SourceChannel", sourceChannelCol, 100);
    header.addColumn ("TargetNote",    targetNoteCol,    120);
    header.addColumn ("TargetChannel", targetChannelCol, 100);
}

void MappingTableComponent::resized()
{
    auto area = getLocalBounds();

    auto buttons = area.removeFromBottom (30).reduced (0, 2);
    const int buttonGap = 6;
    const int buttonWidth = 80;

    // Right side: Add / Remove / Clear
    removeButton.setBounds (buttons.removeFromRight (buttonWidth));
    buttons.removeFromRight (buttonGap);
    clearButton.setBounds (buttons.removeFromRight (buttonWidth));
    buttons.removeFromRight (buttonGap);
    addButton.setBounds (buttons.removeFromRight (buttonWidth));
    buttons.removeFromRight (buttonGap);

    // Left side: Import / Export
    importButton.setBounds (buttons.removeFromLeft (buttonWidth));
    buttons.removeFromLeft (buttonGap);
    exportButton.setBounds (buttons.removeFromLeft (buttonWidth));

    table.setBounds (area);

    // Manually distribute column widths. We deliberately use slightly less
    // than the full width so the last column is never clipped — critical for
    // high-DPI (e.g. 150%) where JUCE's internal width calculations drift.
    auto& header = table.getHeader();
    const int avail = juce::jmax (100, area.getWidth() - 4);  // 4px safety margin

    // Percentages: 15 + 22 + 18 + 22 + 18 = 95  (remaining 5% stays empty)
    header.setColumnWidth (nameCol,           avail * 15 / 100);
    header.setColumnWidth (sourceNoteCol,     avail * 22 / 100);
    header.setColumnWidth (sourceChannelCol,  avail * 18 / 100);
    header.setColumnWidth (targetNoteCol,     avail * 22 / 100);
    header.setColumnWidth (targetChannelCol,  avail * 18 / 100);
}

void MappingTableComponent::buttonClicked (juce::Button* button)
{
    if (button == &addButton)
    {
        processor.addEntry();
    }
    else if (button == &removeButton)
    {
        const int selected = table.getSelectedRow();
        if (selected >= 0)
            processor.removeEntry (selected);
    }
    else if (button == &clearButton)
    {
        processor.clearEntries();
    }
    else if (button == &importButton)
    {
        juce::FileChooser fc ("Import Drum Map", {}, "*.bwdrm;*.drm");

        if (fc.browseForFileToOpen())
        {
            auto file = fc.getResult();
            if (processor.importFromFile (file))
            {
                // Success
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon,
                    "Import Failed",
                    "Could not import the selected file.\nMake sure it is a valid .bwdrm file.");
            }
        }
    }
    else if (button == &exportButton)
    {
        juce::FileChooser fc ("Export Drum Map", {}, "*.bwdrm");

        if (fc.browseForFileToSave (true))
        {
            auto file = fc.getResult().withFileExtension ("bwdrm");
            if (! processor.exportToFile (file))
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon,
                    "Export Failed",
                    "Could not write to the selected file.");
            }
        }
    }

    refresh();
}

//==============================================================================
int MappingTableComponent::getNumRows()
{
    return processor.getNumEntries();
}

void MappingTableComponent::paintRowBackground (juce::Graphics& g, int /*rowNumber*/, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll (juce::Colours::steelblue.withAlpha (0.3f));
    else
        g.fillAll (juce::Colours::darkgrey.withAlpha (0.2f));

    g.setColour (juce::Colours::grey.withAlpha (0.4f));
    g.drawRect (0, 0, width, height);
}

void MappingTableComponent::paintCell (juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool /*rowIsSelected*/)
{
    // Custom components cover the editable cells, so this is just a fallback.
    g.setColour (juce::Colours::white);

    const auto entry = processor.getEntry (rowNumber);

    juce::String text;
    switch (columnId)
    {
        case nameCol:          text = entry.name; break;
        case sourceNoteCol:    text = NoteNameUtils::midiToNameWithNumber (entry.sourceNote); break;
        case sourceChannelCol: text = NoteNameUtils::channelToString (entry.sourceChannel); break;
        case targetNoteCol:    text = NoteNameUtils::midiToNameWithNumber (entry.targetNote); break;
        case targetChannelCol: text = NoteNameUtils::channelToString (entry.targetChannel); break;
        default: break;
    }

    g.drawText (text, 6, 0, width - 6, height, juce::Justification::centredLeft);
}

juce::Component* MappingTableComponent::refreshComponentForCell (int rowNumber, int columnId, bool /*isRowSelected*/, juce::Component* existingToUpdate)
{
    switch (columnId)
    {
        case nameCol:
        {
            auto* c = dynamic_cast<NameCellComponent*> (existingToUpdate);
            if (c == nullptr)
                c = new NameCellComponent (processor);
            c->setRow (rowNumber);
            return c;
        }
        case sourceNoteCol:
        {
            auto* c = dynamic_cast<NoteComboCell*> (existingToUpdate);
            if (c == nullptr)
                c = new NoteComboCell (processor, true);
            c->setRow (rowNumber);
            return c;
        }
        case targetNoteCol:
        {
            auto* c = dynamic_cast<NoteComboCell*> (existingToUpdate);
            if (c == nullptr)
                c = new NoteComboCell (processor, false);
            c->setRow (rowNumber);
            return c;
        }
        case sourceChannelCol:
        {
            auto* c = dynamic_cast<ChannelComboCell*> (existingToUpdate);
            if (c == nullptr)
                c = new ChannelComboCell (processor, true);
            c->setRow (rowNumber);
            return c;
        }
        case targetChannelCol:
        {
            auto* c = dynamic_cast<ChannelComboCell*> (existingToUpdate);
            if (c == nullptr)
                c = new ChannelComboCell (processor, false);
            c->setRow (rowNumber);
            return c;
        }
        default:
            break;
    }

    return nullptr;
}

void MappingTableComponent::refresh()
{
    table.updateContent();
    table.repaint();
}
