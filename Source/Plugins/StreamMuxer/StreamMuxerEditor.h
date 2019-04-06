#include <EditorHeaders.h>

namespace StreamMuxerPlugin
{
	class StreamGroup;

	class StreamMuxerEditor : public GenericEditor, public ComboBoxListener
	{
	public:
		StreamMuxerEditor(GenericProcessor* parentNode, bool useDefaultParameterEditors);
		~StreamMuxerEditor();
		
		void comboBoxChanged(ComboBox* combo) override;

		void setStreamGroups(std::vector<StreamGroup>& groups, int selectedGroup, int selectedStream);

		void startAcquisition() override;

		void stopAcquisition() override;
	private:
		ScopedPointer<ComboBox> groupCombo;
		ScopedPointer<ComboBox> streamCombo;
		ScopedPointer<Label> groupLabel;
		ScopedPointer<Label> streamLabel;
	};

}