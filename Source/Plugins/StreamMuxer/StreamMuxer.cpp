#include "StreamMuxer.h"
#include "StreamMuxerEditor.h"
#include <math.h>

using namespace StreamMuxerPlugin;

bool StreamGroup::operator==(const StreamGroup& other) const
{
	return (numChannels == other.numChannels && sampleRate == other.sampleRate && !isnan(sampleRate));
}

StreamGroup::StreamGroup() : sampleRate(NAN), numChannels(0)
{
}

StreamGroup::StreamGroup(float s, int n) : sampleRate(s), numChannels(n)
{
}

StreamMuxer::StreamMuxer() : GenericProcessor("Stream Muxer")
{
	m_selectedGroup = -1;
	m_selectedStream.set(-1);
	m_selectedGroupChanged = false;
	setProcessorType(Plugin::PluginProcessorType::PROCESSOR_TYPE_FILTER);
}


StreamMuxer::~StreamMuxer()
{
}

AudioProcessorEditor* StreamMuxer::createEditor()
{
	editor = new StreamMuxerEditor(this, true);
	return editor;
}


void StreamMuxer::updateSettings()
{
	//if the update came from other part of the signal chain, update everything
	//we do not do this process when updating from the same processor to avoid wasting resources
	if (!m_selectedGroupChanged)
	{
		//save current selections in case the inputs remain valid
		StreamGroup savedGroup;
		if (m_selectedGroup >= 0 && m_selectedGroup < streamGroups.size())
		{
			savedGroup.numChannels = streamGroups[m_selectedGroup].numChannels;
			savedGroup.sampleRate = streamGroups[m_selectedGroup].sampleRate;
		}

		streamGroups.clear();

		int numChannels = getNumInputs();
		uint16 curProc = 0;
		uint16 curSub = 0;
		StreamGroup workingGroup;
		int startOffset = -1;
		for (int i = 0; i < numChannels; i++)
		{
			const DataChannel* channel = dataChannelArray[i];
			//stream change
			//this assumes that all channels from the same stream are contiguous. A channel mapper mixing streams will break this
			if (curProc != channel->getSourceNodeID() || curSub != channel->getSubProcessorIdx())
			{
				curProc = channel->getSourceNodeID();
				curSub = channel->getSubProcessorIdx();

				insertGroup(workingGroup, startOffset);

				//reset group
				workingGroup.numChannels = 0;
				workingGroup.sampleRate = channel->getSampleRate();
				workingGroup.startOffsets.clear();
				startOffset = i;
			}
			workingGroup.numChannels++;
		}
		insertGroup(workingGroup, startOffset); //last group insertion

		//now check if the selected group is still valid
		if (m_selectedGroup >= 0 && m_selectedGroup < streamGroups.size() && streamGroups[m_selectedGroup] == savedGroup)
		{
			//group is valid, check for stream validity
			int stream = m_selectedStream.get();
			if (stream > 0 && stream >= streamGroups[m_selectedGroup].startOffsets.size())
			{
				//not valid, setting stream to first one
				m_selectedStream.set(0);
			}
		}
		else
		{
			//chose the first option, to be safe
			m_selectedGroup = 0;
			m_selectedStream.set(0);
		}
		//update editor
		static_cast<StreamMuxerEditor*>(getEditor())->setStreamGroups(streamGroups, m_selectedGroup, m_selectedStream.get());
	}
#if 0
	{
		for (int g = 0; g < streamGroups.size(); g++)
		{
			std::cout << "g " << g << " s " << streamGroups[g].sampleRate << " n " << streamGroups[g].numChannels << " on " << streamGroups[g].startOffsets.size() << std::endl;
			for (int o = 0; o < streamGroups[g].startOffsets.size(); o++)
			{
				std::cout << "o " << o << " - " << streamGroups[g].startOffsets[o] << std::endl;
			}
		}
	}
#endif
	//Now we update the output channels
	{
		OwnedArray<DataChannel> oldChannels;
		oldChannels.swapWith(dataChannelArray);
		dataChannelArray.clear();

		originalChannels.clear();

		int numChannels = 0;
		float sampleRate = 0;
		if (m_selectedGroup >= 0 && m_selectedGroup < streamGroups.size())
		{
			numChannels = streamGroups[m_selectedGroup].numChannels;
			settings.numOutputs = numChannels;

			sampleRate = streamGroups[m_selectedGroup].sampleRate;

			//store original channels
			std::vector<int>& offsets = streamGroups[m_selectedGroup].startOffsets;
			int numStreams = offsets.size();

			for (int i = 0; i < numStreams; i++)
			{
				originalChannels.push_back(OwnedArray<DataChannel>());
				int offset = offsets[i];
				for (int c = 0; c < numChannels; c++)
				{
					DataChannel* chan = oldChannels[offset+c];
					oldChannels.set(offset+c, nullptr, false);
					originalChannels.back().add(chan);
				}
			}

			//create metadata structures
			String historic = "{";
			HeapBlock<uint16> idArray;
			idArray.allocate(numStreams * 2, true);
			for (int i = 0; i < numStreams; i++)
			{
				DataChannel* chan = originalChannels[i][0]; //take first channel as sample
				historic += "[" + chan->getHistoricString() + "]";
				idArray[2*i] = chan->getSourceNodeID();
				idArray[2 * i + 1] = chan->getSubProcessorIdx();
			}
			historic += "}";
			MetaDataDescriptor mdNumDesc = MetaDataDescriptor(MetaDataDescriptor::UINT32, 1, "Number of muxed streams",
				"Number of streams muxed into this channel",
				"stream.mux.count");
			MetaDataDescriptor mdSourceDesc = MetaDataDescriptor(MetaDataDescriptor::UINT16, numStreams * 2, "Source processors",
				"2xN array of uint16 that specifies the nodeID and Stream index of the possible sources for this channel",
				"source.identifier.full.array");

			MetaDataValue mdNum = MetaDataValue(mdNumDesc);
			mdNum.setValue((uint32)numStreams);
			MetaDataValue mdSource = MetaDataValue(mdSourceDesc, idArray.getData());

			for (int i = 0; i < numChannels; i++)
			{
				
				//lets assume the values are consistent between streams and take the first stream as sample
				DataChannel* orig = originalChannels.front()[i];
				DataChannel::DataChannelTypes type = orig->getChannelType();
				DataChannel* chan = new DataChannel(type, sampleRate, this);
				chan->setBitVolts(orig->getBitVolts());
				chan->setDataUnits(orig->getDataUnits());
				
				chan->addToHistoricString(historic);
				chan->addMetaData(mdNumDesc, mdNum);
				chan->addMetaData(mdSourceDesc, mdSource);
				
				dataChannelArray.add(chan);
			}
			m_selectedSampleRate = sampleRate;
			m_selectedBitVolts = dataChannelArray[0]->getBitVolts(); //to get some value
		}
	}
	m_selectedGroupChanged = false; //always reset this
	//now create event channel

	EventChannel* ech = new EventChannel(EventChannel::UINT32_ARRAY, 1, 1, m_selectedSampleRate, this);
	ech->setName("Stream Selected");
	ech->setDescription("Value of the selected stream each time it changes");
	ech->setIdentifier("stream.mux.index.selected");
	eventChannelArray.add(ech);
	m_ech = ech;
}

void StreamMuxer::insertGroup(StreamGroup& workingGroup, int startOffset)
{
	//find if there is a group with the characteristics we had
	int numGroups = streamGroups.size();
	for (int i = 0; i < numGroups; i++)
	{
		if (streamGroups[i] == workingGroup)
		{
			streamGroups[i].startOffsets.push_back(startOffset);
			startOffset = -1;
			break;
		}
	}
	//if new group or first group
	if (startOffset > -1)
	{
		workingGroup.startOffsets.push_back(startOffset);
		streamGroups.push_back(workingGroup);
	}
}

void StreamMuxer::setParameter(int parameterIndex, float value)
{
	if (parameterIndex == 0) //stream group
	{
		m_selectedGroup = value;
		m_selectedGroupChanged = true;
	}
	else if (parameterIndex == 1)
	{
		m_selectedStream.set(value);
	}
}

float StreamMuxer::getDefaultSampleRate() const
{
	return m_selectedSampleRate;
}

float StreamMuxer::getDefaultBitVolts() const
{
	return m_selectedBitVolts;
}

bool StreamMuxer::enable()
{
	m_oldSelectedStream = m_selectedStream.get();
	m_firstBlock = true;
	m_lastTimestamp = -1;
	return true;
}

void StreamMuxer::process(AudioSampleBuffer& buffer)
{
	uint32 selectedStream = m_selectedStream.get();

	DataChannel* chan = originalChannels[selectedStream][0];
	uint32 selectedSamples = getNumSourceSamples(chan->getSourceNodeID(), chan->getSubProcessorIdx());
	uint64 selectedTimestamp = getSourceTimestamp(chan->getSourceNodeID(), chan->getSubProcessorIdx());
	uint32 samplenum = 0;

	if (selectedStream == m_oldSelectedStream) //nothing has changed, proceed normally
	{
		performBufferCopy(buffer, selectedStream, 0, 0, selectedSamples);
		setTimestampAndSamples(selectedTimestamp, selectedSamples);
	}
	else //do some tricks to align stuff
	{
		uint64 eventTimestamp = selectedTimestamp;

		DataChannel* lchan = originalChannels[m_oldSelectedStream][0];
		//uint32 lastSelSamples = getNumSourceSamples(lchan->getSourceNodeID(), lchan->getSubProcessorIdx());
		uint64 lastSelTimestamp = getSourceTimestamp(lchan->getSourceNodeID(), lchan->getSubProcessorIdx());

		if (lastSelTimestamp != m_lastTimestamp) //there has been data loss. Do not attempt to align and just continue normally
		{
			performBufferCopy(buffer, selectedStream, 0, 0, selectedSamples);
			setTimestampAndSamples(selectedTimestamp, selectedSamples);
		}
		else
		{
			int64 timestampDiff = selectedTimestamp - lastSelTimestamp;
	//		std::cout << "timestamp diff " << timestampDiff << std::endl;
			if ((timestampDiff > 0) //this block has a positive offset, copy part of the previous stream
				&& (selectedSamples + timestampDiff <= buffer.getNumSamples())) //assuming there is enough space. if not, simply an apparent data loss will occur
			{
				//first copy the previously selected stream
				performBufferCopy(buffer, m_oldSelectedStream, 0, 0, timestampDiff);
				//now the ones from the current buffer
				performBufferCopy(buffer, selectedStream, timestampDiff, 0, selectedSamples);
				setTimestampAndSamples(lastSelTimestamp, selectedSamples + timestampDiff);
				samplenum = timestampDiff;
			}
			else //negative offset. we need to trim this block
			{
				if (timestampDiff > 0) timestampDiff = 0; 
				timestampDiff = -timestampDiff; //make it positive now to be easier to read
				uint32 numSamples = selectedSamples - timestampDiff;
				performBufferCopy(buffer, selectedStream, 0, timestampDiff, numSamples);
				setTimestampAndSamples(selectedTimestamp + timestampDiff, numSamples);
				eventTimestamp = selectedTimestamp + timestampDiff;
			}
		}
		//send event to signal stream switch
		BinaryEventPtr ev = BinaryEvent::createBinaryEvent(m_ech, eventTimestamp, &selectedStream, sizeof(selectedStream));
		addEvent(m_ech, ev, samplenum);
	}

	if (m_firstBlock) //send event at start
	{
		BinaryEventPtr ev = BinaryEvent::createBinaryEvent(m_ech, selectedTimestamp, &selectedStream, sizeof(selectedStream));
		addEvent(m_ech, ev, 0);
		m_firstBlock = false;
	}
	m_lastTimestamp = selectedTimestamp + selectedSamples;;
	m_oldSelectedStream = selectedStream;

	
}

void StreamMuxer::performBufferCopy(AudioSampleBuffer& buffer, int stream, uint32 destStartSample, uint32 sourceStartSample, uint32 numSamples)
{
	int channelOffset = streamGroups[m_selectedGroup].startOffsets[stream];
	for (int i = 0; i < settings.numOutputs; i++)
	{
		if (channelOffset != 0)
		{
			buffer.copyFrom(i, //destChannel
				destStartSample, //destStartSample
				buffer, //sourceBuffer
				channelOffset + i, //sourceChannel
				sourceStartSample, //sourceStartSample
				numSamples);
		}
	}
}