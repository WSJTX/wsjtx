#include "MessageServer.hpp"

#include <stdexcept>
#include <limits>

#include <QNetworkInterface>
#include <QUdpSocket>
#include <QTimer>
#include <QHash>

#include "Radio.hpp"
#include "Network/NetworkMessage.hpp"
#include "qt_helpers.hpp"

#include "pimpl_impl.hpp"

#include "moc_MessageServer.cpp"

namespace
{
  auto quint32_max = std::numeric_limits<quint32>::max ();
}

class MessageServer::impl
  : public QUdpSocket
{
  Q_OBJECT;

public:
  impl (MessageServer * self, QString const& version, QString const& revision)
    : self_ {self}
    , version_ {version}
    , revision_ {revision}
    , clock_ {new QTimer {this}}
  {
    // register the required types with Qt
    Radio::register_types ();

    connect (this, &QIODevice::readyRead, this, &MessageServer::impl::pending_datagrams);
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    connect (this, static_cast<void (impl::*) (SocketError)> (&impl::error)
             , [this] (SocketError /* e */)
             {
               Q_EMIT self_->error (errorString ());
             });
#else
    connect (this, &impl::errorOccurred, [this] (SocketError /* e */)
                                 {
                                   Q_EMIT self_->error (errorString ());
                                 });
#endif
    connect (clock_, &QTimer::timeout, this, &impl::tick);
    clock_->start (NetworkMessage::pulse * 1000);
  }

  enum StreamStatus {Fail, Short, OK};

  void leave_multicast_group ();
  void join_multicast_group ();
  void parse_message (QHostAddress const& sender, port_type sender_port, QByteArray const& msg);
  void tick ();
  void pending_datagrams ();
  StreamStatus check_status (QDataStream const&) const;
  void send_message (QDataStream const& out, QByteArray const& message, QHostAddress const& address, port_type port)
  {
      if (OK == check_status (out))
        {
          writeDatagram (message, address, port);
        }
      else
        {
          Q_EMIT self_->error ("Error creating UDP message");
        }
  }

  MessageServer * self_;
  QString version_;
  QString revision_;
  QHostAddress multicast_group_address_;
  QSet<QString> network_interfaces_;
  static BindMode constexpr bind_mode_ = ShareAddress | ReuseAddressHint;
  struct Client
  {
    Client () = default;
    Client (port_type const& sender_port)
      : sender_port_ {sender_port}
      , negotiated_schema_number_ {2} // not 1 because it's broken
      , last_activity_ {QDateTime::currentDateTime ()}
    {
    }
    Client (Client const&) = default;
    Client& operator= (Client const&) = default;

    port_type sender_port_;
    quint32 negotiated_schema_number_;
    QDateTime last_activity_;
  };
  QHash<ClientKey, Client> clients_; // maps id to Client
  QTimer * clock_;
};

MessageServer::impl::BindMode constexpr MessageServer::impl::bind_mode_;

#include "MessageServer.moc"

void MessageServer::impl::leave_multicast_group ()
{
  if (BoundState == state () && is_multicast_address (multicast_group_address_))
    {
      for (auto const& if_name : network_interfaces_)
        {
          leaveMulticastGroup (multicast_group_address_, QNetworkInterface::interfaceFromName (if_name));
        }
    }
}

void MessageServer::impl::join_multicast_group ()
{
  if (BoundState == state () && is_multicast_address (multicast_group_address_))
    {
      if (network_interfaces_.size ())
        {
          for (auto const& if_name : network_interfaces_)
            {
              joinMulticastGroup (multicast_group_address_, QNetworkInterface::interfaceFromName (if_name));
            }
        }
      else
        {
          // find the loop-back interface and join on that
          for (auto const& net_if : QNetworkInterface::allInterfaces ())
            {
              auto flags = QNetworkInterface::IsUp | QNetworkInterface::IsLoopBack | QNetworkInterface::CanMulticast;
              if ((net_if.flags () & flags) == flags)
                {
                  joinMulticastGroup (multicast_group_address_, net_if);
                  break;
                }
            }
        }
    }
}

void MessageServer::impl::pending_datagrams ()
{
  while (hasPendingDatagrams ())
    {
      QByteArray datagram;
      datagram.resize (pendingDatagramSize ());
      QHostAddress sender_address;
      port_type sender_port;
      if (0 <= readDatagram (datagram.data (), datagram.size (), &sender_address, &sender_port))
        {
          parse_message (sender_address, sender_port, datagram);
        }
    }
}

void MessageServer::impl::parse_message (QHostAddress const& sender, port_type sender_port, QByteArray const& msg)
{
  try
    {
      //
      // message format is described in NetworkMessage.hpp
      //
      NetworkMessage::Reader in {msg};

      auto id = in.id ();
      if (OK == check_status (in))
        {
          auto client_key = ClientKey {sender, id};
          if (!clients_.contains (client_key))
            {
              auto& client = (clients_[client_key] = {sender_port});
              QByteArray client_version;
              QByteArray client_revision;

              if (NetworkMessage::Heartbeat == in.type ())
                {
                  // negotiate a working schema number
                  in >> client.negotiated_schema_number_;
                  if (OK == check_status (in))
                    {
                      auto sn = NetworkMessage::Builder::schema_number;
                      client.negotiated_schema_number_ = std::min (sn, client.negotiated_schema_number_);

                      // reply to the new client informing it of the
                      // negotiated schema number
                      QByteArray message;
                      NetworkMessage::Builder hb {&message, NetworkMessage::Heartbeat, id, client.negotiated_schema_number_};
                      hb << NetworkMessage::Builder::schema_number // maximum schema number accepted
                         << version_.toUtf8 () << revision_.toUtf8 ();
                      if (impl::OK == check_status (hb))
                        {
                          writeDatagram (message, client_key.first, sender_port);
                        }
                      else
                        {
                          Q_EMIT self_->error ("Error creating UDP message");
                        }
                    }
                  // we don't care if this fails to read
                  in >> client_version >> client_revision;
                }
              Q_EMIT self_->client_opened (client_key, QString::fromUtf8 (client_version),
                                           QString::fromUtf8 (client_revision));
            }
          clients_[client_key].last_activity_ = QDateTime::currentDateTime ();
  
          //
          // message format is described in NetworkMessage.hpp
          //
          switch (in.type ())
            {
            case NetworkMessage::Heartbeat:
              //nothing to do here as time out handling deals with lifetime
              break;

            case NetworkMessage::Clear:
              Q_EMIT self_->decodes_cleared (client_key);
              break;

            case NetworkMessage::Status:
              {
                // unpack message
                Frequency f;
                QByteArray mode;
                QByteArray dx_call;
                QByteArray report;
                QByteArray tx_mode;
                bool tx_enabled {false};
                bool transmitting {false};
                bool decoding {false};
                quint32 rx_df {quint32_max};
                quint32 tx_df {quint32_max};
                QByteArray de_call;
                QByteArray de_grid;
                QByteArray dx_grid;
                bool watchdog_timeout {false};
                QByteArray sub_mode;
                bool fast_mode {false};
                quint8 special_op_mode {0};
                quint32 frequency_tolerance {quint32_max};
                quint32 tr_period {quint32_max};
                QByteArray configuration_name;
                QByteArray tx_message;
                in >> f >> mode >> dx_call >> report >> tx_mode >> tx_enabled >> transmitting >> decoding
                   >> rx_df >> tx_df >> de_call >> de_grid >> dx_grid >> watchdog_timeout >> sub_mode
                   >> fast_mode >> special_op_mode >> frequency_tolerance >> tr_period >> configuration_name
                   >> tx_message;
                if (check_status (in) != Fail)
                  {
                    Q_EMIT self_->status_update (client_key, f, QString::fromUtf8 (mode)
                                                 , QString::fromUtf8 (dx_call)
                                                 , QString::fromUtf8 (report), QString::fromUtf8 (tx_mode)
                                                 , tx_enabled, transmitting, decoding, rx_df, tx_df
                                                 , QString::fromUtf8 (de_call), QString::fromUtf8 (de_grid)
                                                 , QString::fromUtf8 (dx_grid), watchdog_timeout
                                                 , QString::fromUtf8 (sub_mode), fast_mode
                                                 , special_op_mode, frequency_tolerance, tr_period
                                                 , QString::fromUtf8 (configuration_name)
                                                 , QString::fromUtf8 (tx_message));
                  }
              }
              break;

            case NetworkMessage::Decode:
              {
                // unpack message
                bool is_new {true};
                QTime time;
                qint32 snr;
                float delta_time;
                quint32 delta_frequency;
                QByteArray mode;
                QByteArray message;
                bool low_confidence {false};
                bool off_air {false};
                in >> is_new >> time >> snr >> delta_time >> delta_frequency >> mode
                   >> message >> low_confidence >> off_air;
                if (check_status (in) != Fail)
                  {
                    Q_EMIT self_->decode (is_new, client_key, time, snr, delta_time, delta_frequency
                                          , QString::fromUtf8 (mode), QString::fromUtf8 (message)
                                          , low_confidence, off_air);
                  }
              }
              break;

            case NetworkMessage::WSPRDecode:
              {
                // unpack message
                bool is_new {true};
                QTime time;
                qint32 snr;
                float delta_time;
                Frequency frequency;
                qint32 drift;
                QByteArray callsign;
                QByteArray grid;
                qint32 power;
                bool off_air {false};
                in >> is_new >> time >> snr >> delta_time >> frequency >> drift >> callsign >> grid >> power
                   >> off_air;
                if (check_status (in) != Fail)
                  {
                    Q_EMIT self_->WSPR_decode (is_new, client_key, time, snr, delta_time, frequency, drift
                                               , QString::fromUtf8 (callsign), QString::fromUtf8 (grid)
                                               , power, off_air);
                  }
              }
              break;

            case NetworkMessage::QSOLogged:
              {
                QDateTime time_off;
                QByteArray dx_call;
                QByteArray dx_grid;
                Frequency dial_frequency;
                QByteArray mode;
                QByteArray report_sent;
                QByteArray report_received;
                QByteArray tx_power;
                QByteArray comments;
                QByteArray name;
                QDateTime time_on; // Note: LOTW uses TIME_ON for their +/- 30-minute time window
                QByteArray operator_call;
                QByteArray my_call;
                QByteArray my_grid;
                QByteArray exchange_sent;
                QByteArray exchange_rcvd;
                QByteArray prop_mode;
                in >> time_off >> dx_call >> dx_grid >> dial_frequency >> mode >> report_sent >> report_received
                   >> tx_power >> comments >> name >> time_on >> operator_call >> my_call >> my_grid
                   >> exchange_sent >> exchange_rcvd >> prop_mode;
                if (check_status (in) != Fail)
                  {
                    Q_EMIT self_->qso_logged (client_key, time_off, QString::fromUtf8 (dx_call)
                                              , QString::fromUtf8 (dx_grid)
                                              , dial_frequency, QString::fromUtf8 (mode)
                                              , QString::fromUtf8 (report_sent)
                                              , QString::fromUtf8 (report_received), QString::fromUtf8 (tx_power)
                                              , QString::fromUtf8 (comments), QString::fromUtf8 (name), time_on
                                              , QString::fromUtf8 (operator_call), QString::fromUtf8 (my_call)
                                              , QString::fromUtf8 (my_grid), QString::fromUtf8 (exchange_sent)
                                              , QString::fromUtf8 (exchange_rcvd), QString::fromUtf8 (prop_mode));
                  }
              }
              break;

            case NetworkMessage::Close:
              Q_EMIT self_->client_closed (client_key);
              clients_.remove (client_key);
              break;

            case NetworkMessage::LoggedADIF:
              {
                QByteArray ADIF;
                in >> ADIF;
                if (check_status (in) != Fail)
                  {
                    Q_EMIT self_->logged_ADIF (client_key, ADIF);
                  }
              }
              break;

            default:
              // Ignore
              break;
            }
        }
      else
        {
          Q_EMIT self_->error ("MessageServer warning: invalid UDP message received");
        }
    }
  catch (std::exception const& e)
    {
      Q_EMIT self_->error (QString {"MessageServer exception: %1"}.arg (e.what ()));
    }
  catch (...)
    {
      Q_EMIT self_->error ("Unexpected exception in MessageServer");
    }
}

void MessageServer::impl::tick ()
{
  auto now = QDateTime::currentDateTime ();
  auto iter = std::begin (clients_);
  while (iter != std::end (clients_))
    {
      if (now > (*iter).last_activity_.addSecs (NetworkMessage::pulse))
        {
          Q_EMIT self_->decodes_cleared (iter.key ());
          Q_EMIT self_->client_closed (iter.key ());
          iter = clients_.erase (iter); // safe while iterating as doesn't rehash
        }
      else
        {
          ++iter;
        }
    }
}

auto MessageServer::impl::check_status (QDataStream const& stream) const -> StreamStatus
{
  auto stat = stream.status ();
  StreamStatus result {Fail};
  switch (stat)
    {
    case QDataStream::ReadPastEnd:
      result = Short;
      break;

    case QDataStream::ReadCorruptData:
      Q_EMIT self_->error ("Message serialization error: read corrupt data");
      break;

    case QDataStream::WriteFailed:
      Q_EMIT self_->error ("Message serialization error: write error");
      break;

    default:
      result = OK;
      break;
    }
  return result;
}

MessageServer::MessageServer (QObject * parent, QString const& version, QString const& revision)
  : QObject {parent}
  , m_ {this, version, revision}
{
}

void MessageServer::start (port_type port, QHostAddress const& multicast_group_address
                           , QSet<QString> const& network_interface_names)
{
  // qDebug () << "MessageServer::start port:" << port << "multicast addr:" << multicast_group_address.toString () << "network interfaces:" << network_interface_names;
  if (port != m_->localPort ()
      || multicast_group_address != m_->multicast_group_address_
      || network_interface_names != m_->network_interfaces_)
    {
      m_->leave_multicast_group ();
      if (impl::UnconnectedState != m_->state ())
        {
          m_->close ();
        }
      if (!(multicast_group_address.isNull () || is_multicast_address (multicast_group_address)))
        {
          Q_EMIT error ("Invalid multicast group address");
        }
      else if (is_MAC_ambiguous_multicast_address (multicast_group_address))
        {
          Q_EMIT error ("MAC-ambiguous IPv4 multicast group address not supported");
        }
      else
        {
          m_->multicast_group_address_ = multicast_group_address;
          m_->network_interfaces_ = network_interface_names;
          QHostAddress local_addr {is_multicast_address (multicast_group_address)
                                   && impl::IPv4Protocol == multicast_group_address.protocol () ? QHostAddress::AnyIPv4 : QHostAddress::Any};
          if (port && m_->bind (local_addr, port, m_->bind_mode_))
            {
              m_->join_multicast_group ();
            }
        }
    }
}

void MessageServer::clear_decodes (ClientKey const& key, quint8 window)
{
  auto iter = m_->clients_.find (key);
  if (iter != std::end (m_->clients_))
    {
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::Clear, key.second, (*iter).negotiated_schema_number_};
      out << window;
      m_->send_message (out, message, key.first, (*iter).sender_port_);
    }
}

void MessageServer::reply (ClientKey const& key, QTime time, qint32 snr, float delta_time
                           , quint32 delta_frequency, QString const& mode
                           , QString const& message_text, bool low_confidence, quint8 modifiers)
{
  auto iter = m_->clients_.find (key);
  if (iter != std::end (m_->clients_))
    {
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::Reply, key.second, (*iter).negotiated_schema_number_};
      out << time << snr << delta_time << delta_frequency << mode.toUtf8 ()
          << message_text.toUtf8 () << low_confidence << modifiers;
      m_->send_message (out, message, key.first, (*iter).sender_port_);
    }
}

void MessageServer::replay (ClientKey const& key)
{
  auto iter = m_->clients_.find (key);
  if (iter != std::end (m_->clients_))
    {
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::Replay, key.second, (*iter).negotiated_schema_number_};
      m_->send_message (out, message, key.first, (*iter).sender_port_);
    }
}

void MessageServer::close (ClientKey const& key)
{
  auto iter = m_->clients_.find (key);
  if (iter != std::end (m_->clients_))
    {
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::Close, key.second, (*iter).negotiated_schema_number_};
      m_->send_message (out, message, key.first, (*iter).sender_port_);
    }
}

void MessageServer::halt_tx (ClientKey const& key, bool auto_only)
{
  auto iter = m_->clients_.find (key);
  if (iter != std::end (m_->clients_))
    {
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::HaltTx, key.second, (*iter).negotiated_schema_number_};
      out << auto_only;
      m_->send_message (out, message, key.first, (*iter).sender_port_);
    }
}

void MessageServer::free_text (ClientKey const& key, QString const& text, bool send)
{
  auto iter = m_->clients_.find (key);
  if (iter != std::end (m_->clients_))
    {
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::FreeText, key.second, (*iter).negotiated_schema_number_};
      out << text.toUtf8 () << send;
      m_->send_message (out, message, key.first, (*iter).sender_port_);
    }
}

void MessageServer::location (ClientKey const& key, QString const& loc)
{
  auto iter = m_->clients_.find (key);
  if (iter != std::end (m_->clients_))
  {
    QByteArray message;
    NetworkMessage::Builder out {&message, NetworkMessage::Location, key.second, (*iter).negotiated_schema_number_};
    out << loc.toUtf8 ();
    m_->send_message (out, message, key.first, (*iter).sender_port_);
  }
}

void MessageServer::highlight_callsign (ClientKey const& key, QString const& callsign
                                        , QColor const& bg, QColor const& fg, bool last_only)
{
  auto iter = m_->clients_.find (key);
  if (iter != std::end (m_->clients_))
  {
    QByteArray message;
    NetworkMessage::Builder out {&message, NetworkMessage::HighlightCallsign, key.second, (*iter).negotiated_schema_number_};
    out << callsign.toUtf8 () << bg << fg << last_only;
    m_->send_message (out, message, key.first, (*iter).sender_port_);
  }
}

void MessageServer::switch_configuration (ClientKey const& key, QString const& configuration_name)
{
  auto iter = m_->clients_.find (key);
  if (iter != std::end (m_->clients_))
  {
    QByteArray message;
    NetworkMessage::Builder out {&message, NetworkMessage::SwitchConfiguration, key.second, (*iter).negotiated_schema_number_};
    out << configuration_name.toUtf8 ();
    m_->send_message (out, message, key.first, (*iter).sender_port_);
  }
}

void MessageServer::configure (ClientKey const& key, QString const& mode, quint32 frequency_tolerance
                               , QString const& submode, bool fast_mode, quint32 tr_period, quint32 rx_df
                               , QString const& dx_call, QString const& dx_grid, bool generate_messages)
{
  auto iter = m_->clients_.find (key);
  if (iter != std::end (m_->clients_))
  {
    QByteArray message;
    NetworkMessage::Builder out {&message, NetworkMessage::Configure, key.second, (*iter).negotiated_schema_number_};
    out << mode.toUtf8 () << frequency_tolerance << submode.toUtf8 () << fast_mode << tr_period << rx_df
        << dx_call.toUtf8 () << dx_grid.toUtf8 () << generate_messages;
    m_->send_message (out, message, key.first, (*iter).sender_port_);
  }
}
