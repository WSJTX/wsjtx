#ifndef WSJTX_UDP_DECODES_MODEL_HPP__
#define WSJTX_UDP_DECODES_MODEL_HPP__

#include <QStandardItemModel>

#include "MessageServer.hpp"

class QTime;
class QString;
class QModelIndex;

//
// Decodes Model - simple data model for all decodes
//
// The model is a basic table with uniform row format. Rows consist of
// QStandardItem instances containing the string representation of the
// column data  and if the underlying  field is not a  string then the
// UserRole+1 role contains the underlying data item.
//
// Three slots  are provided to add  a new decode, remove  all decodes
// for a client  and, to build a  reply to CQ message for  a given row
// which is emitted as a signal respectively.
//
class DecodesModel
  : public QStandardItemModel
{
  Q_OBJECT;

  using ClientKey = MessageServer::ClientKey;

public:
  explicit DecodesModel (QObject * parent = nullptr);

  Q_SLOT void add_decode (bool is_new, ClientKey const&, QTime, qint32 snr, float delta_time
                          , quint32 delta_frequency, QString const& mode, QString const& message
                          , bool low_confidence, bool off_air, bool is_fast);
  Q_SLOT void decodes_cleared (ClientKey const&);
  Q_SLOT void do_reply (QModelIndex const& source, quint8 modifiers);

  Q_SIGNAL void reply (ClientKey const&, QTime, qint32 snr, float delta_time, quint32 delta_frequency
                       , QString const& mode, QString const& message, bool low_confidence, quint8 modifiers);
};

#endif
