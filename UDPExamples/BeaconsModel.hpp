#ifndef WSJTX_UDP_BEACONS_MODEL_HPP__
#define WSJTX_UDP_BEACONS_MODEL_HPP__

#include <QStandardItemModel>

#include "MessageServer.hpp"

using Frequency = MessageServer::Frequency;

class QString;
class QTime;

//
// Beacons Model - simple data model for all beacon spots
//
// The model is a basic table with uniform row format. Rows consist of
// QStandardItem instances containing the string representation of the
// column data  and if the underlying  field is not a  string then the
// UserRole+1 role contains the underlying data item.
//
// Two slots are provided to add a new decode and remove all spots for
// a client.
//
class BeaconsModel
  : public QStandardItemModel
{
  Q_OBJECT;

  using ClientKey = MessageServer::ClientKey;

public:
  explicit BeaconsModel (QObject * parent = nullptr);

  Q_SLOT void add_beacon_spot (bool is_new, ClientKey const&, QTime time, qint32 snr, float delta_time
                               , Frequency frequency, qint32 drift, QString const& callsign, QString const& grid
                               , qint32 power, bool off_air);
  Q_SLOT void decodes_cleared (ClientKey const&);
};

#endif
