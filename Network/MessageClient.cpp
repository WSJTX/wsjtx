#include "MessageClient.hpp"

#include <stdexcept>
#include <vector>
#include <algorithm>
#include <limits>

#include <QUdpSocket>
#include <QNetworkInterface>
#include <QHostInfo>
#include <QTimer>
#include <QElapsedTimer>
#include <QQueue>
#include <QByteArray>
#include <QColor>
#include <QDebug>

#include "NetworkMessage.hpp"
#include "qt_helpers.hpp"
#include "pimpl_impl.hpp"

#include "moc_MessageClient.cpp"

// some trace macros
#if WSJT_TRACE_UDP
#define TRACE_UDP(MSG) qDebug () << QString {"MessageClient::%1:"}.arg (__func__) << MSG
#else
#define TRACE_UDP(MSG)
#endif

namespace
{
  int constexpr replay_interval_ms {10};
  int constexpr replay_write_budget {10};
  int constexpr replay_message_budget {10};
  qint64 constexpr replay_work_budget_ms {2};
  int constexpr decode_history_limit {5000};
}

class MessageClient::impl
  : public QUdpSocket
{
  Q_OBJECT;

public:
  impl (QString const& id, QString const& version, QString const& revision,
        port_type server_port, int TTL, MessageClient * self)
    : self_ {self}
    , enabled_ {false}
    , id_ {id}
    , version_ {version}
    , revision_ {revision}
    , dns_lookup_id_ {-1}
    , server_port_ {server_port}
    , TTL_ {TTL}
    , schema_ {2}  // use 2 prior to negotiation not 1 which is broken
    , replay_state_ {ReplayState::Idle}
    , heartbeat_timer_ {new QTimer {this}}
    , replay_timer_ {new QTimer {this}}
    , replay_generation_ {0}
  {
    connect (heartbeat_timer_, &QTimer::timeout, this, &impl::heartbeat);
    replay_timer_->setSingleShot (true);
    replay_timer_->setTimerType (Qt::PreciseTimer);
    connect (replay_timer_, &QTimer::timeout, this, &impl::drain_replay);
    connect (this, &QIODevice::readyRead, this, &impl::pending_datagrams);

    heartbeat_timer_->start (NetworkMessage::pulse * 1000);
  }

  ~impl ()
  {
    closedown ();
    if (dns_lookup_id_ != -1)
      {
        QHostInfo::abortHostLookup (dns_lookup_id_);
      }
  }

  enum StreamStatus {Fail, Short, OK};
  enum class ReplayState {Idle, Collecting, Draining};

  struct PendingMessage
  {
    explicit PendingMessage (QByteArray const& contents)
      : contents {contents}
      , next_interface {0}
    {
    }

    QByteArray contents;
    std::size_t next_interface;
  };

  struct DecodeIdentity
  {
    QTime time;
    qint32 snr;
    float delta_time;
    quint32 delta_frequency;
    QByteArray mode;
    QByteArray message;
    bool low_confidence;

    bool operator== (DecodeIdentity const& other) const
    {
      return time == other.time && snr == other.snr && delta_time == other.delta_time
        && delta_frequency == other.delta_frequency && mode == other.mode
        && message == other.message && low_confidence == other.low_confidence;
    }
  };

  void set_server (QString const& server_name, QStringList const& network_interface_names);
  Q_SLOT void host_info_results (QHostInfo);
  void start ();
  void parse_message (QByteArray const&);
  void pending_datagrams ();
  void heartbeat ();
  void closedown ();
  bool begin_replay ();
  void end_replay ();
  void cancel_replay ();
  void drain_replay ();
  StreamStatus check_status (QDataStream const&) const;
  void send_message (QByteArray const&, bool queue_if_pending = true, bool allow_duplicates = false);
  void send_message_immediately (QByteArray const&);
  void remember_decode (DecodeIdentity const&);
  bool is_prior_decode (DecodeIdentity const&) const;
  void clear_decode_history ();
  void send_message (QDataStream const& out, QByteArray const& message, bool queue_if_pending = true, bool allow_duplicates = false)
  {
    if (OK == check_status (out))
      {
        send_message (message, queue_if_pending, allow_duplicates);
      }
    else
      {
        Q_EMIT self_->error ("Error creating UDP message");
      }
  }

  MessageClient * self_;
  bool enabled_;
  QString id_;
  QString version_;
  QString revision_;
  int dns_lookup_id_;
  QHostAddress server_;
  port_type server_port_;
  int TTL_;
  std::vector<QNetworkInterface> network_interfaces_;
  quint32 schema_;
  ReplayState replay_state_;
  QTimer * heartbeat_timer_;
  QTimer * replay_timer_;
  quint64 replay_generation_;
  std::vector<QHostAddress> blocked_addresses_;

  // hold messages sent before host lookup completes asynchronously
  QQueue<QByteArray> pending_messages_;
  QQueue<PendingMessage> replay_messages_;
  QQueue<DecodeIdentity> decode_history_;
  QByteArray last_message_;
};

#include "MessageClient.moc"

void MessageClient::impl::set_server (QString const& server_name, QStringList const& network_interface_names)
{
  cancel_replay ();
  // qDebug () << "MessageClient server:" << server_name << "port:" << server_port_ << "interfaces:" << network_interface_names;
  server_.setAddress (server_name);
  network_interfaces_.clear ();
  for (auto const& net_if_name : network_interface_names)
    {
      network_interfaces_.push_back (QNetworkInterface::interfaceFromName (net_if_name));
    }

  if (server_.isNull () && server_name.size ()) // DNS lookup required
    {
      // queue a host address lookup
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
      dns_lookup_id_ = QHostInfo::lookupHost (server_name, this, &MessageClient::impl::host_info_results);
#else
      dns_lookup_id_ = QHostInfo::lookupHost (server_name, this, SLOT (host_info_results (QHostInfo)));
#endif
    }
  else
    {
      start ();
    }
}

void MessageClient::impl::host_info_results (QHostInfo host_info)
{
  if (host_info.lookupId () != dns_lookup_id_) return;
  dns_lookup_id_ = -1;
  if (QHostInfo::NoError != host_info.error ())
    {
      Q_EMIT self_->error ("UDP server DNS lookup failed: " + host_info.errorString ());
      return;
    }
  else
    {
      auto const& server_addresses = host_info.addresses ();
      if (server_addresses.size ())
        {
          server_ = server_addresses[0];
        }
    }
  start ();
}

void MessageClient::impl::start ()
{
  if (server_.isNull ())
    {
      Q_EMIT self_->close ();
      pending_messages_.clear (); // discard
      return;
    }

  if (is_broadcast_address (server_))
    {
      Q_EMIT self_->error ("IPv4 broadcast not supported, please specify the loop-back address, a server host address, or multicast group address");
      pending_messages_.clear (); // discard
      return;
    }

  if (blocked_addresses_.end () != std::find (blocked_addresses_.begin (), blocked_addresses_.end (), server_))
    {
      Q_EMIT self_->error ("UDP server blocked, please try another");
      pending_messages_.clear (); // discard
      return;
    }

  TRACE_UDP ("Trying server:" << server_.toString ());
  QHostAddress interface_addr {IPv6Protocol == server_.protocol () ? QHostAddress::AnyIPv6 : QHostAddress::AnyIPv4};

  if (localAddress () != interface_addr)
    {
      if (UnconnectedState != state () || state ())
        {
          close ();
        }
      // bind to an ephemeral port on the selected interface and set
      // up for sending datagrams
      bind (interface_addr);
      // qDebug () << "Bound to UDP port:" << localPort () << "on:" << localAddress ();

      // set multicast TTL to limit scope when sending to multicast
      // group addresses
      setSocketOption (MulticastTtlOption, TTL_);
    }

  // send initial heartbeat which allows schema negotiation
  heartbeat ();

  // clear any backlog
  while (pending_messages_.size ())
    {
      send_message (pending_messages_.dequeue (), true, false);
    }
}

void MessageClient::impl::pending_datagrams ()
{
  while (hasPendingDatagrams ())
    {
      QByteArray datagram;
      datagram.resize (pendingDatagramSize ());
      QHostAddress sender_address;
      port_type sender_port;
      if (0 <= readDatagram (datagram.data (), datagram.size (), &sender_address, &sender_port))
        {
          TRACE_UDP ("message received from:" << sender_address << "port:" << sender_port);
          parse_message (datagram);
        }
    }
}

void MessageClient::impl::parse_message (QByteArray const& msg)
{
  try
    {
      // 
      // message format is described in NetworkMessage.hpp
      // 
      NetworkMessage::Reader in {msg};
      if (OK == check_status (in))
        {
          if (schema_ < in.schema ()) // one time record of server's
                                      // negotiated schema
            {
              schema_ = in.schema ();
            }

          if (!enabled_)
            {
              TRACE_UDP ("message processing disabled for id:" << in.id ());
              return;
            }

          //
          // message format is described in NetworkMessage.hpp
          //
          switch (in.type ())
            {
            case NetworkMessage::Reply:
              {
                // unpack message
                QTime time;
                qint32 snr;
                float delta_time;
                quint32 delta_frequency;
                QByteArray mode;
                QByteArray message;
                bool low_confidence {false};
                quint8 modifiers {0};
                in >> time >> snr >> delta_time >> delta_frequency >> mode >> message
                   >> low_confidence >> modifiers;
                TRACE_UDP ("Reply: time:" << time << "snr:" << snr << "dt:" << delta_time << "df:" << delta_frequency << "mode:" << mode << "message:" << message << "low confidence:" << low_confidence << "modifiers: 0x"
#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
                           << Qt::hex
#else
                           << hex
#endif
                           << modifiers);
                if (check_status (in) != Fail)
                  {
                    DecodeIdentity const decode {time, snr, delta_time, delta_frequency,
                                                 mode, message, low_confidence};
                    if (is_prior_decode (decode))
                      {
                        Q_EMIT self_->reply (time, snr, delta_time, delta_frequency
                                             , QString::fromUtf8 (mode), QString::fromUtf8 (message)
                                             , low_confidence, modifiers);
                      }
                    else
                      {
                        qDebug () << "process reply message ignored, decode not found:"
                                  << time << snr << delta_time << delta_frequency << mode << message;
                      }
                  }
              }
              break;

            case NetworkMessage::Clear:
              {
                quint8 window {0};
                in >> window;
                TRACE_UDP ("Clear window:" << window);
                if (check_status (in) != Fail)
                  {
                    if (window == 0 || window == 2)
                      {
                        clear_decode_history ();
                      }
                    Q_EMIT self_->clear_decodes (window);
                  }
              }
              break;

            case NetworkMessage::Close:
              TRACE_UDP ("Close");
              if (check_status (in) != Fail)
                {
                  last_message_.clear ();
                  Q_EMIT self_->close ();
                }
              break;

            case NetworkMessage::Replay:
              TRACE_UDP ("Replay");
              if (check_status (in) != Fail)
                {
                  Q_EMIT self_->replay ();
                }
              break;

            case NetworkMessage::HaltTx:
              {
                bool auto_only {false};
                in >> auto_only;
                TRACE_UDP ("Halt Tx auto_only:" << auto_only);
                if (check_status (in) != Fail)
                  {
                    Q_EMIT self_->halt_tx (auto_only);
                  }
              }
              break;

            case NetworkMessage::FreeText:
              {
                QByteArray message;
                bool send {true};
                in >> message >> send;
                TRACE_UDP ("FreeText message:" << message << "send:" << send);
                if (check_status (in) != Fail)
                  {
                    Q_EMIT self_->free_text (QString::fromUtf8 (message), send);
                  }
              }
              break;

            case NetworkMessage::Location:
              {
                QByteArray location;
                in >> location;
                TRACE_UDP ("Location location:" << location);
                if (check_status (in) != Fail)
                {
                    Q_EMIT self_->location (QString::fromUtf8 (location));
                }
              }
              break;

            case NetworkMessage::HighlightCallsign:
              {
                QByteArray call;
                QColor bg;      // default invalid color
                QColor fg;      // default invalid color
                bool last_only {false};
                in >> call >> bg >> fg >> last_only;
                TRACE_UDP ("HighlightCallsign call:" << call << "bg:" << bg << "fg:" << fg << "last only:" << last_only);
                if (check_status (in) != Fail && call.size ())
                  {
                    Q_EMIT self_->highlight_callsign (QString::fromUtf8 (call), bg, fg, last_only);
                  }
              }
              break;

            case NetworkMessage::SwitchConfiguration:
              {
                QByteArray configuration_name;
                in >> configuration_name;
                TRACE_UDP ("Switch Configuration name:" << configuration_name);
                if (check_status (in) != Fail)
                  {
                    Q_EMIT self_->switch_configuration (QString::fromUtf8 (configuration_name));
                  }
              }
              break;

            case NetworkMessage::Configure:
              {
                QByteArray mode;
                quint32 frequency_tolerance;
                QByteArray submode;
                bool fast_mode {false};
                quint32 tr_period {std::numeric_limits<quint32>::max ()};
                quint32 rx_df {std::numeric_limits<quint32>::max ()};
                QByteArray dx_call;
                QByteArray dx_grid;
                bool generate_messages {false};
                in >> mode >> frequency_tolerance >> submode >> fast_mode >> tr_period >> rx_df
                   >> dx_call >> dx_grid >> generate_messages;
                TRACE_UDP ("Configure mode:" << mode << "frequency tolerance:" << frequency_tolerance << "submode:" << submode << "fast mode:" << fast_mode << "T/R period:" << tr_period << "rx df:" << rx_df << "dx call:" << dx_call << "dx grid:" << dx_grid << "generate messages:" << generate_messages);
                if (check_status (in) != Fail)
                  {
                    Q_EMIT self_->configure (QString::fromUtf8 (mode), frequency_tolerance
                                             , QString::fromUtf8 (submode), fast_mode, tr_period, rx_df
                                             , QString::fromUtf8 (dx_call), QString::fromUtf8 (dx_grid)
                                             , generate_messages);
                  }
              }
              break;

            case NetworkMessage::AnnotationInfo: {
              QByteArray dx_call;
              bool sort_order_provided{false};
              quint32 sort_order{std::numeric_limits<quint32>::max()};
              in >> dx_call >> sort_order_provided >> sort_order;
              TRACE_UDP ("External Callsign Info:" << dx_call << "sort_order_provided:" << sort_order_provided
                                                  << "sort_order:" << sort_order);
              if (sort_order > 50000) sort_order = 50000;
              if (check_status(in) != Fail) {
                Q_EMIT
                  self_->annotation_info(QString::fromUtf8(dx_call), sort_order_provided, sort_order);
              }
            }
            break;

            default:
              // Ignore
              //
              // Note that although server  heartbeat messages are not
              // parsed here  they are  still partially parsed  in the
              // message reader class to  negotiate the maximum schema
              // number being used on the network.
              if (NetworkMessage::Heartbeat != in.type ())
                {
                  TRACE_UDP ("ignoring message type:" << in.type ());
                }
              break;
            }
        }
      else
        {
          TRACE_UDP ("ignored message for id:" << in.id ());
        }
    }
  catch (std::exception const& e)
    {
      Q_EMIT self_->error (QString {"MessageClient exception: %1"}.arg (e.what ()));
    }
  catch (...)
    {
      Q_EMIT self_->error ("Unexpected exception in MessageClient");
    }
}

void MessageClient::impl::remember_decode (DecodeIdentity const& decode)
{
  if (decode_history_.size () >= decode_history_limit)
    {
      decode_history_.dequeue ();
    }
  decode_history_.enqueue (decode);
}

bool MessageClient::impl::is_prior_decode (DecodeIdentity const& decode) const
{
  return decode_history_.contains (decode);
}

void MessageClient::impl::clear_decode_history ()
{
  decode_history_.clear ();
}

void MessageClient::impl::heartbeat ()
{
   if (server_port_ && !server_.isNull ())
    {
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::Heartbeat, id_, schema_};
      out << NetworkMessage::Builder::schema_number // maximum schema number accepted
          << version_.toUtf8 () << revision_.toUtf8 ();
      TRACE_UDP ("schema:" << schema_ << "max schema:" << NetworkMessage::Builder::schema_number << "version:" << version_ << "revision:" << revision_);
      send_message (out, message, false, true);
    }
}

void MessageClient::impl::closedown ()
{
  cancel_replay ();
   if (server_port_ && !server_.isNull ())
    {
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::Close, id_, schema_};
      TRACE_UDP ("");
      send_message (out, message, false);
    }
}

bool MessageClient::impl::begin_replay ()
{
  if (ReplayState::Idle != replay_state_)
    {
      return false;
    }

  last_message_.clear ();
  replay_state_ = ReplayState::Collecting;
  return true;
}

void MessageClient::impl::end_replay ()
{
  if (ReplayState::Collecting != replay_state_)
    {
      return;
    }

  if (replay_messages_.isEmpty ())
    {
      replay_state_ = ReplayState::Idle;
      return;
    }

  replay_state_ = ReplayState::Draining;
  replay_timer_->start (replay_interval_ms);
}

void MessageClient::impl::cancel_replay ()
{
  ++replay_generation_;
  replay_timer_->stop ();
  replay_messages_.clear ();
  replay_state_ = ReplayState::Idle;
}

void MessageClient::impl::drain_replay ()
{
  if (ReplayState::Draining != replay_state_)
    {
      return;
    }

  auto const generation = replay_generation_;
  QElapsedTimer work_timer;
  work_timer.start ();
  int writes {0};
  int messages {0};

  while (!replay_messages_.isEmpty ()
         && writes < replay_write_budget
         && messages < replay_message_budget
         && work_timer.elapsed () < replay_work_budget_ms)
    {
      auto const contents = replay_messages_.head ().contents;
      if (is_multicast_address (server_))
        {
          auto const next_interface = replay_messages_.head ().next_interface;
          if (next_interface < network_interfaces_.size ())
            {
              if (generation != replay_generation_ || ReplayState::Draining != replay_state_)
                {
                  return;
                }
              setMulticastInterface (network_interfaces_[next_interface]);
              writeDatagram (contents, server_, server_port_);
              if (generation != replay_generation_ || ReplayState::Draining != replay_state_)
                {
                  return;
                }
              ++replay_messages_.head ().next_interface;
              ++writes;
            }

          if (replay_messages_.head ().next_interface < network_interfaces_.size ())
            {
              continue;
            }
        }
      else
        {
          if (generation != replay_generation_ || ReplayState::Draining != replay_state_)
            {
              return;
            }
          writeDatagram (contents, server_, server_port_);
          if (generation != replay_generation_ || ReplayState::Draining != replay_state_)
            {
              return;
            }
          ++writes;
        }

      last_message_ = contents;
      replay_messages_.dequeue ();
      ++messages;
    }

  if (replay_messages_.isEmpty ())
    {
      replay_state_ = ReplayState::Idle;
    }
  else
    {
      replay_timer_->start (replay_interval_ms);
    }

  if (messages)
    {
      Q_EMIT self_->replay_batch_processed (messages);
    }
}

void MessageClient::impl::send_message (QByteArray const& message, bool queue_if_pending, bool allow_duplicates)
{
  if (server_port_)
    {
      if (!server_.isNull ())
        {
          auto const& previous_message = replay_messages_.isEmpty () ? last_message_ : replay_messages_.back ().contents;
          if (allow_duplicates || message != previous_message) // avoid duplicates
            {
              if (ReplayState::Idle != replay_state_)
                {
                  replay_messages_.enqueue (PendingMessage {message});
                }
              else
                {
                  send_message_immediately (message);
                }
            }
        }
      else if (queue_if_pending)
        {
          pending_messages_.enqueue (message);
        }
    }
}

void MessageClient::impl::send_message_immediately (QByteArray const& message)
{
  if (is_multicast_address (server_))
    {
      for (auto const& net_if : network_interfaces_)
        {
          setMulticastInterface (net_if);
          writeDatagram (message, server_, server_port_);
        }
    }
  else
    {
      writeDatagram (message, server_, server_port_);
    }
  last_message_ = message;
}

auto MessageClient::impl::check_status (QDataStream const& stream) const -> StreamStatus
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

MessageClient::MessageClient (QString const& id, QString const& version, QString const& revision,
                              QString const& server_name, port_type server_port,
                              QStringList const& network_interface_names,
                              int TTL, QObject * self)
  : QObject {self}
  , m_ {id, version, revision, server_port, TTL, this}
{
  connect (&*m_
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
           , static_cast<void (impl::*) (impl::SocketError)> (&impl::error), [this] (impl::SocketError e)
#else
           , &impl::errorOccurred, [this] (impl::SocketError e)
#endif
                                   {
#if defined (Q_OS_WIN)
                                     if (e != impl::NetworkError // take this out when Qt 5.5 stops doing this spuriously
                                         && e != impl::ConnectionRefusedError) // not interested in this with UDP socket
                                       {
#else
                                       {
                                         Q_UNUSED (e);
#endif
                                         Q_EMIT error (m_->errorString ());
                                       }
                                       });
  m_->set_server (server_name, network_interface_names);
}

QHostAddress MessageClient::server_address () const
{
  return m_->server_;
}

auto MessageClient::server_port () const -> port_type
{
  return m_->server_port_;
}

void MessageClient::set_server (QString const& server_name, QStringList const& network_interface_names)
{
  m_->set_server (server_name, network_interface_names);
}

void MessageClient::set_server_port (port_type server_port)
{
  if (m_->server_port_ != server_port)
    {
      m_->cancel_replay ();
      m_->server_port_ = server_port;
    }
}

void MessageClient::set_TTL (int TTL)
{
  m_->TTL_ = TTL;
  m_->setSocketOption (QAbstractSocket::MulticastTtlOption, m_->TTL_);
}

void MessageClient::enable (bool flag)
{
  m_->enabled_ = flag;
}

bool MessageClient::begin_replay ()
{
  return m_->begin_replay ();
}

void MessageClient::end_replay ()
{
  m_->end_replay ();
}

void MessageClient::status_update (Frequency f, QString const& mode, QString const& dx_call
                                   , QString const& report, QString const& tx_mode
                                   , bool tx_enabled, bool transmitting, bool decoding
                                   , quint32 rx_df, quint32 tx_df, QString const& de_call
                                   , QString const& de_grid, QString const& dx_grid
                                   , bool watchdog_timeout, QString const& sub_mode
                                   , bool fast_mode, quint8 special_op_mode
                                   , quint32 frequency_tolerance, quint32 tr_period
                                   , QString const& configuration_name
                                   , QString const& tx_message)
{
  if (m_->server_port_ && !m_->server_.isNull ())
    {
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::Status, m_->id_, m_->schema_};
      out << f << mode.toUtf8 () << dx_call.toUtf8 () << report.toUtf8 () << tx_mode.toUtf8 ()
          << tx_enabled << transmitting << decoding << rx_df << tx_df << de_call.toUtf8 ()
          << de_grid.toUtf8 () << dx_grid.toUtf8 () << watchdog_timeout << sub_mode.toUtf8 ()
          << fast_mode << special_op_mode << frequency_tolerance << tr_period << configuration_name.toUtf8 ()
          << tx_message.toUtf8 ();
      TRACE_UDP ("frequency:" << f << "mode:" << mode << "DX:" << dx_call << "report:" << report << "Tx mode:" << tx_mode << "tx_enabled:" << tx_enabled << "Tx:" << transmitting << "decoding:" << decoding << "Rx df:" << rx_df << "Tx df:" << tx_df << "DE:" << de_call << "DE grid:" << de_grid << "DX grid:" << dx_grid << "w/d t/o:" << watchdog_timeout << "sub_mode:" << sub_mode << "fast mode:" << fast_mode << "spec op mode:" << special_op_mode << "frequency tolerance:" << frequency_tolerance << "T/R period:" << tr_period << "configuration name:" << configuration_name << "Tx message:" << tx_message);
      m_->send_message (out, message);
    }
}

void MessageClient::decode (bool is_new, QTime time, qint32 snr, float delta_time, quint32 delta_frequency
                            , QString const& mode, QString const& message_text, bool low_confidence
                            , bool off_air)
{
   if (m_->server_port_ && !m_->server_.isNull ())
    {
      auto const mode_utf8 = mode.toUtf8 ();
      auto const message_utf8 = message_text.toUtf8 ();
      m_->remember_decode ({time, snr, delta_time, delta_frequency, mode_utf8,
                            message_utf8, low_confidence});
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::Decode, m_->id_, m_->schema_};
      out << is_new << time << snr << delta_time << delta_frequency << mode_utf8
          << message_utf8 << low_confidence << off_air;
      TRACE_UDP ("new" << is_new << "time:" << time << "snr:" << snr << "dt:" << delta_time << "df:" << delta_frequency << "mode:" << mode << "text:" << message_text << "low conf:" << low_confidence << "off air:" << off_air);
      m_->send_message (out, message);
    }
}

void MessageClient::WSPR_decode (bool is_new, QTime time, qint32 snr, float delta_time, Frequency frequency
                                 , qint32 drift, QString const& callsign, QString const& grid, qint32 power
                                 , bool off_air)
{
   if (m_->server_port_ && !m_->server_.isNull ())
    {
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::WSPRDecode, m_->id_, m_->schema_};
      out << is_new << time << snr << delta_time << frequency << drift << callsign.toUtf8 ()
          << grid.toUtf8 () << power << off_air;
      TRACE_UDP ("new:" << is_new << "time:" << time << "snr:" << snr << "dt:" << delta_time << "frequency:" << frequency << "drift:" << drift << "call:" << callsign << "grid:" << grid << "pwr:" << power << "off air:" << off_air);
      m_->send_message (out, message);
    }
}

void MessageClient::decodes_cleared ()
{
   m_->clear_decode_history ();
   if (m_->server_port_ && !m_->server_.isNull ())
    {
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::Clear, m_->id_, m_->schema_};
      TRACE_UDP ("");
      m_->send_message (out, message);
    }
}

void MessageClient::qso_logged (QDateTime time_off, QString const& dx_call, QString const& dx_grid
                                , Frequency dial_frequency, QString const& mode, QString const& report_sent
                                , QString const& report_received, QString const& tx_power
                                , QString const& comments, QString const& name, QDateTime time_on
                                , QString const& operator_call, QString const& my_call
                                , QString const& my_grid, QString const& exchange_sent
                                , QString const& exchange_rcvd, QString const& propmode
                                , QString const& satellite, QString const& satmode, QString const& freqRx)
{
   if (m_->server_port_ && !m_->server_.isNull ())
    {
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::QSOLogged, m_->id_, m_->schema_};
      out << time_off << dx_call.toUtf8 () << dx_grid.toUtf8 () << dial_frequency << mode.toUtf8 ()
          << report_sent.toUtf8 () << report_received.toUtf8 () << tx_power.toUtf8 () << comments.toUtf8 ()
          << name.toUtf8 () << time_on << operator_call.toUtf8 () << my_call.toUtf8 () << my_grid.toUtf8 ()
          << exchange_sent.toUtf8 () << exchange_rcvd.toUtf8 () << propmode.toUtf8 () << satellite.toUtf8 ()
          << satmode.toUtf8 () << freqRx.toUtf8 ();
      TRACE_UDP ("time off:" << time_off << "DX:" << dx_call << "DX grid:" << dx_grid << "dial:" << dial_frequency << "mode:" << mode << "sent:" << report_sent << "rcvd:" << report_received << "pwr:" << tx_power << "comments:" << comments << "name:" << name << "time on:" << time_on << "op:" << operator_call << "DE:" << my_call << "DE grid:" << my_grid << "exch sent:" << exchange_sent << "exch rcvd:" << exchange_rcvd  << "prop_mode:" << propmode << "sat_name:" << satellite << "sat_mode:" << satmode << "freq_rx:" << freqRx);
      m_->send_message (out, message);
    }
}

void MessageClient::logged_ADIF (QByteArray const& ADIF_record)
{
   if (m_->server_port_ && !m_->server_.isNull ())
    {
      QByteArray message;
      NetworkMessage::Builder out {&message, NetworkMessage::LoggedADIF, m_->id_, m_->schema_};
      QByteArray ADIF {"\n<adif_ver:5>3.1.0\n<programid:6>WSJT-X\n<EOH>\n" + ADIF_record + " <EOR>"};
      out << ADIF;
      TRACE_UDP ("ADIF:" << ADIF);
      m_->send_message (out, message);
    }
}
