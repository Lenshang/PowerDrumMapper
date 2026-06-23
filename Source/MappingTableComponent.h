#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class DrumMapperAudioProcessor;

/**
    The editable mapping table.

    Columns: | Name | SourceNote | SourceChannel | TargetNote | TargetChannel |

    Note and channel cells are combo boxes (note names are shown as "C0 (12)"
    etc., channels as "ALL" / "1".."16"). The Name cell is an editable label.
    Add / Remove / Clear buttons live below the table.
*/
class MappingTableComponent
    : public juce::Component
    , public juce::TableListBoxModel
    , public juce::Button::Listener
{
public:
    enum ColumnIds
    {
        nameCol = 1,
        sourceNoteCol,
        sourceChannelCol,
        targetNoteCol,
        targetChannelCol
    };

    explicit MappingTableComponent (DrumMapperAudioProcessor& processorToUse);
    ~MappingTableComponent() override;

    void resized() override;

    void buttonClicked (juce::Button*) override;

    // ---- TableListBoxModel ----
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell (juce::Graphics&, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    juce::Component* refreshComponentForCell (int rowNumber, int columnId, bool isRowSelected, juce::Component* existingToUpdate) override;

    /** Rebuild the table rows from the processor's current mapping. */
    void refresh();

private:
    DrumMapperAudioProcessor& processor;

    juce::TableListBox table { {}, this };
    juce::TextButton addButton    { "Add" };
    juce::TextButton removeButton { "Remove" };
    juce::TextButton clearButton  { "Clear" };
    juce::TextButton importButton { "Import..." };
    juce::TextButton exportButton { "Export..." };

    void setupColumns();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MappingTableComponent)
};
