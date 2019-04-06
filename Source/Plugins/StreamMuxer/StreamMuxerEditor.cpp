#include "StreamMuxer.h"
#include "StreamMuxerEditor.h"

using namespace StreamMuxerPlugin;

StreamMuxerEditor::StreamMuxerEditor(GenericProcessor* parentNode, bool useDefaultParameterEditors)
	: GenericEditor(parentNode, useDefaultParameterEditors)
{
	desiredWidth = 260;

	groupLabel = new Label("Group Label", "Stream Group:");
	groupLabel->setBounds(10, 25, 120, 20);
	groupLabel->setFont(Font("Small Text", 12, Font::plain));
	groupLabel->setColour(Label::textColourId, Colours::darkgrey);
	addAndMakeVisible(groupLabel);

	streamLabel = new Label("Stream Label", "Stream:");
	streamLabel->setBounds(10, 65, 80, 20);
	streamLabel->setFont(Font("Small Text", 12, Font::plain));
	streamLabel->setColour(Label::textColourId, Colours::darkgrey);
	addAndMakeVisible(streamLabel);

	groupCombo = new ComboBox("Group Combo");
	groupCombo->setBounds(120, 25, 120, 20);
	groupCombo->addListener(this);
	addAndMakeVisible(groupCombo);

	streamCombo = new ComboBox("Stream Combo");
	streamCombo->setBounds(120, 65, 120, 20);
	streamCombo->addListener(this);
	addAndMakeVisible(streamCombo);
}

StreamMuxerEditor::~StreamMuxerEditor()
{
}

void StreamMuxerEditor::comboBoxChanged(ComboBox* combo)
{
	if (combo == groupCombo)
	{
		getProcessor()->setParameter(0, groupCombo->getSelectedId() - 1);
		CoreServices::updateSignalChain(this);
	}
	else if (combo == streamCombo)
	{
		getProcessor()->setParameter(1, streamCombo->getSelectedId() - 1);
	}
}

void StreamMuxerEditor::startAcquisition()
{
	groupCombo->setEnabled(false);
}

void StreamMuxerEditor::stopAcquisition()
{
	groupCombo->setEnabled(true);
}

void StreamMuxerEditor::setStreamGroups(std::vector<StreamGroup>& groups, int selectedGroup, int selectedStream)
{
	groupCombo->clear(dontSendNotification);
	streamCombo->clear(dontSendNotification);

	int numGroups = groups.size();
	for (int i = 0; i < numGroups; i++)
	{
		String text = String(groups[i].numChannels) + "ch@" + String(groups[i].sampleRate, 2) + "Hz";
		groupCombo->addItem(text, i + 1);
	}
	if (selectedGroup >= 0 && selectedGroup < groups.size())
	{
		int numStreams = groups[selectedGroup].startOffsets.size();
		for (int i = 0; i < numStreams; i++)
		{
			streamCombo->addItem(String(i + 1), i + 1);
		}
	}
	groupCombo->setSelectedId(selectedGroup + 1, dontSendNotification);
	streamCombo->setSelectedId(selectedStream + 1, dontSendNotification);
}