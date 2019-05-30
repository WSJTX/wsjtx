#include "MetaDataRegistry.hpp"

#include <QMetaType>
#include <QItemEditorFactory>
#include <QStandardItemEditorCreator>

#include "Radio.hpp"
#include "models/FrequencyList.hpp"
#include "AudioDevice.hpp"
#include "Configuration.hpp"
#include "models/StationList.hpp"
#include "Transceiver.hpp"
#include "TransceiverFactory.hpp"
#include "WFPalette.hpp"
#include "models/IARURegions.hpp"
#include "models/DecodeHighlightingModel.hpp"
#include "widgets/DateTimeEdit.hpp"

namespace
{
  class ItemEditorFactory final
    : public QItemEditorFactory
  {
  public:
    ItemEditorFactory ()
      : default_factory_ {QItemEditorFactory::defaultFactory ()}
    {
    }

    QWidget * createEditor (int user_type, QWidget * parent) const override
    {
      auto editor = QItemEditorFactory::createEditor (user_type, parent);
      return editor ? editor : default_factory_->createEditor (user_type, parent);
    }

  private:
    QItemEditorFactory const * default_factory_;
  };
}

void register_types ()
{
  auto item_editor_factory = new ItemEditorFactory;
  QItemEditorFactory::setDefaultFactory (item_editor_factory);

  // types in Radio.hpp are registered in their own translation unit
  // as they are needed in the wsjtx_udp shared library too

  // we still have to register the fully qualified names of enum types
  // used as signal/slot connection arguments since the new Qt 5.5
  // Q_ENUM macro only seems to register the unqualified name
  
  item_editor_factory->registerEditor (qMetaTypeId<QDateTime> (), new QStandardItemEditorCreator<DateTimeEdit> ());

  // Frequency list model
  qRegisterMetaTypeStreamOperators<FrequencyList_v2::Item> ("Item_v2");
  qRegisterMetaTypeStreamOperators<FrequencyList_v2::FrequencyItems> ("FrequencyItems_v2");

  // defunct old versions
  qRegisterMetaTypeStreamOperators<FrequencyList::Item> ("Item");
  qRegisterMetaTypeStreamOperators<FrequencyList::FrequencyItems> ("FrequencyItems");

  // Audio device
  qRegisterMetaType<AudioDevice::Channel> ("AudioDevice::Channel");

  // Configuration
  qRegisterMetaTypeStreamOperators<Configuration::DataMode> ("Configuration::DataMode");
  qRegisterMetaTypeStreamOperators<Configuration::Type2MsgGen> ("Configuration::Type2MsgGen");

  // Station details
  qRegisterMetaType<StationList::Station> ("Station");
  qRegisterMetaType<StationList::Stations> ("Stations");
  qRegisterMetaTypeStreamOperators<StationList::Station> ("Station");
  qRegisterMetaTypeStreamOperators<StationList::Stations> ("Stations");

  // Transceiver
  qRegisterMetaType<Transceiver::TransceiverState> ("Transceiver::TransceiverState");

  // Transceiver factory
  qRegisterMetaTypeStreamOperators<TransceiverFactory::DataBits> ("TransceiverFactory::DataBits");
  qRegisterMetaTypeStreamOperators<TransceiverFactory::StopBits> ("TransceiverFactory::StopBits");
  qRegisterMetaTypeStreamOperators<TransceiverFactory::Handshake> ("TransceiverFactory::Handshake");
  qRegisterMetaTypeStreamOperators<TransceiverFactory::PTTMethod> ("TransceiverFactory::PTTMethod");
  qRegisterMetaTypeStreamOperators<TransceiverFactory::TXAudioSource> ("TransceiverFactory::TXAudioSource");
  qRegisterMetaTypeStreamOperators<TransceiverFactory::SplitMode> ("TransceiverFactory::SplitMode");

  // Waterfall palette
  qRegisterMetaTypeStreamOperators<WFPalette::Colours> ("Colours");

  // IARURegions
  qRegisterMetaTypeStreamOperators<IARURegions::Region> ("IARURegions::Region");

  // DecodeHighlightingModel
  qRegisterMetaTypeStreamOperators<DecodeHighlightingModel::HighlightInfo> ("HighlightInfo");
  qRegisterMetaTypeStreamOperators<DecodeHighlightingModel::HighlightItems> ("HighlightItems");
}
