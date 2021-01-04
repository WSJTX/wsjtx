#ifndef MESSAGE_CLIENT_HPP__
#define MESSAGE_CLIENT_HPP__

#include <QObject>
#include <QTime>
#include <QDateTime>
#include <QString>
#include <QHostAddress>

#include "Radio.hpp"
#include "pimpl_h.hpp"

class QByteArray;
class QHostAddress;
class QColor;

//
// MessageClient - Manage messages sent and replies received from a
//                 matching server (MessageServer) at the other end of
//                 the wire
//
//
// Each outgoing message type is a Qt slot
//
class MessageClient
  : public QObject
{
  Q_OBJECT;

public:
  using Frequency = Radio::Frequency;
  using port_type = quint16;

  // instantiate and initiate a host lookup on the server
  //
  // messages will be silently dropped until a server host lookup is complete
  MessageClient (QString const& id, QString const& version, QString const& revision,
                 QString const& server_name, port_type server_port,
                 QStringList const& network_interface_names,
                 int TTL, QObject * parent = nullptr);

  // query server details
  QHostAddress server_address () const;
  port_type server_port () const;

  // initiate a new server host lookup or if the server name is empty
  // the sending of messages is disabled, if an interface is specified
  // then that interface is used for outgoing datagrams
  Q_SLOT void set_server (QString const& server_name, QStringList const& network_interface_names);

  // change the server port messages are sent to
  Q_SLOT void set_server_port (port_type server_port = 0u);

  // change the server port messages are sent to
  Q_SLOT void set_TTL (int TTL);

  // enable incoming messages
  Q_SLOT void enable (bool);

  // outgoing messages
  Q_SLOT void status_update (Frequency, QString const& mode, QString const& dx_call, QString const& report
                             , QString const& tx_mode, bool tx_enabled, bool transmitting, bool decoding
                             , quint32 rx_df, quint32 tx_df, QString const& de_call, QString const& de_grid
                             , QString const& dx_grid, bool watchdog_timeout, QString const& sub_mode
                             , bool fast_mode, quint8 special_op_mode, quint32 frequency_tolerance
                             , quint32 tr_period, QString const& configuration_name
                             , QString const& tx_message);
  Q_SLOT void decode (bool is_new, QTime time, qint32 snr, float delta_time, quint32 delta_frequency
                      , QString const& mode, QString const& message, bool low_confidence
                      , bool off_air);
  Q_SLOT void WSPR_decode (bool is_new, QTime time, qint32 snr, float delta_time, Frequency
                           , qint32 drift, QString const& callsign, QString const& grid, qint32 power
                           , bool off_air);
  Q_SLOT void decodes_cleared ();
  Q_SLOT void qso_logged (QDateTime time_off, QString const& dx_call, QString const& dx_grid
                          , Frequency dial_frequency, QString const& mode, QString const& report_sent
                          , QString const& report_received, QString const& tx_power, QString const& comments
                          , QString const& name, QDateTime time_on, QString const& operator_call
                          , QString const& my_call, QString const& my_grid
                          , QString const& exchange_sent, QString const& exchange_rcvd
                          , QString const& propmode);

  // ADIF_record argument should be valid ADIF excluding any <EOR> end
  // of record marker
  Q_SLOT void logged_ADIF (QByteArray const& ADIF_record);

  // this signal is emitted if the server has requested a decode
  // window clear action
  Q_SIGNAL void clear_decodes (quint8 window);

  // this signal is emitted if the server sends us a reply, the only
  // reply supported is reply to a prior CQ or QRZ message
  Q_SIGNAL void reply (QTime, qint32 snr, float delta_time, quint32 delta_frequency, QString const& mode
                       , QString const& message_text, bool low_confidence, quint8 modifiers);

  // this signal is emitted if the server has requested this client to
  // close down gracefully
  Q_SIGNAL void close ();

  // this signal is emitted if the server has requested a replay of
  // all decodes
  Q_SIGNAL void replay ();

  // this signal is emitted if the server has requested immediate (or
  // auto Tx if auto_only is true) transmission to halt
  Q_SIGNAL void halt_tx (bool auto_only);

  // this signal is emitted if the server has requested a new free
  // message text
  Q_SIGNAL void free_text (QString const&, bool send);

  // this signal is emitted if the server has sent a highlight
  // callsign request for the specified call
  Q_SIGNAL void highlight_callsign (QString const& callsign, QColor const& bg, QColor const& fg, bool last_only);

  // this signal is emitted if the server has requested a
  // configuration switch
  Q_SIGNAL void switch_configuration (QString const& configuration_name);

  // this signal is emitted if the server has requested a
  // configuration change
  Q_SIGNAL void configure (QString const& mode, quint32 frequency_tolerance, QString const& submode
                           , bool fast_mode, quint32 tr_period, quint32 rx_df, QString const& dx_call
                           , QString const& dx_grid, bool generate_messages);

  // this signal is emitted when network errors occur or if a host
  // lookup fails
  Q_SIGNAL void error (QString const&) const;

  // this signal is emitted if the message obtains a location from a
  // server.  (It doesn't have to be new, could be a periodic location
  // update)
  Q_SIGNAL void location (QString const&);

private:
  class impl;
  pimpl<impl> m_;
};

#endif
