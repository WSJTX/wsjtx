#include "PSKReporter.hpp"

// Interface for posting spots to PSK Reporter web site
// Implemented by Edson Pereira PY2SDR
// Updated by Bill Somerville, G4WJS
//
// Reports will be sent in batch mode every 5 minutes.

#include <fstream>
#include <string>
#include <ctime>
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QSharedPointer>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QQueue>
#include <QTimer>
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
#include <QRandomGenerator>
#endif

#include "Logger.hpp"
#include "PSKReporterIPFIX.hpp"
#include "pimpl_impl.hpp"


#include "moc_PSKReporter.cpp"

//#define DEBUGECLIPSE 0

namespace
{
  QLatin1String HOST {"report.pskreporter.info"};
  // QLatin1String HOST {"127.0.0.1"};
  quint16 SERVICE_PORT {4739};
  // quint16 SERVICE_PORT {14739};
  int MIN_SEND_INTERVAL {120}; // in seconds
  int FLUSH_INTERVAL {MIN_SEND_INTERVAL + 5}; // in send intervals
  int MAX_PENDING_SPOTS {2048};
  int CACHE_TIMEOUT {300}; // default to 5 minutes for repeating spots
  QMap<QString, time_t> spot_cache;

#ifdef DEBUGPSK
  int added;
  int removed;
#endif
}

class PSKReporter::impl final
  : public QObject
{
  Q_OBJECT

  using logger_type = boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level>;

public:
  impl (PSKReporter * self, PSKReporter::Options const& options)
    : logger_ {boost::log::keywords::channel = "PSKRPRT"}
    , self_ {self}
    , options_ {options}
    , sequence_number_ {0u}
    , send_descriptors_ {0}
    , flush_counter_ {0u}
  {
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    observation_id_ = qrand();
#else
    observation_id_ = QRandomGenerator::global ()->generate ();
#endif

    // This timer sets the interval to check for spots to send.
    connect (&report_timer_, &QTimer::timeout, [this] () {send_report ();});

    // UDP has no session state, so templates are repeated periodically in case
    // the collector restarted and lost its template cache.
    connect (&descriptor_timer_, &QTimer::timeout, [this] () {
                                                     if (socket_
                                                         && QAbstractSocket::UdpSocket == socket_->socketType ())
                                                       {
                                                         LOG_LOG_LOCATION (logger_, trace, "enable descriptor resend");
                                                         // send templates again
                                                         send_descriptors_ = 3; // three times
                                                       }
                                                   });
    eclipse_load(options_.eclipse_file_path);
  }

  void check_connection ()
  {
    if (!socket_
        || QAbstractSocket::UnconnectedState == socket_->state ()
        || (socket_->socketType () != (options_.use_tcpip ? QAbstractSocket::TcpSocket : QAbstractSocket::UdpSocket)))
      {
        // we need to create the appropriate socket
        if (socket_
            && QAbstractSocket::UnconnectedState != socket_->state ()
            && QAbstractSocket::ClosingState != socket_->state ())
          {
            LOG_LOG_LOCATION (logger_, trace, "create/recreate socket");
            // handle re-opening asynchronously
            auto connection = QSharedPointer<QMetaObject::Connection>::create ();
            *connection = connect (socket_.data (), &QAbstractSocket::disconnected, [this, connection] () {
                                                                                     disconnect (*connection);
                                                                                     check_connection ();
                                                                                   });
            // close gracefully
            send_report (true);
            socket_->close ();
          }
        else
          {
            reconnect ();
          }
      }
  }

  void handle_socket_error (QAbstractSocket::SocketError e)
  {
    LOG_LOG_LOCATION (logger_, warning, "socket error: " << qPrintable (socket_->errorString ()));
    switch (e)
      {
      case QAbstractSocket::RemoteHostClosedError:
        socket_->disconnectFromHost ();
        break;

      case QAbstractSocket::TemporaryError:
        break;

      default:
        spots_.clear ();
        Q_EMIT self_->errorOccurred (socket_->errorString ());
        break;
      }
  }

  void reconnect ()
  {
    // Using deleteLater for the deleter as we may eventually
    // be called from the disconnected handler above.
    if (options_.use_tcpip)
      {
        LOG_LOG_LOCATION (logger_, trace, "create TCP/IP socket");
        socket_.reset (new QTcpSocket, &QObject::deleteLater);
        send_descriptors_ = 1;
      }
    else
      {
        LOG_LOG_LOCATION (logger_, trace, "create UDP/IP socket");
        socket_.reset (new QUdpSocket, &QObject::deleteLater);
        send_descriptors_ = 3;
      }

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect (socket_.get (), &QAbstractSocket::errorOccurred, this, &PSKReporter::impl::handle_socket_error);
#elif QT_VERSION >= QT_VERSION_CHECK(5, 7, 0)
    connect (socket_.data (), QOverload<QAbstractSocket::SocketError>::of (&QAbstractSocket::error), this, &PSKReporter::impl::handle_socket_error);
#else
    connect (socket_.data (), static_cast<void (QAbstractSocket::*) (QAbstractSocket::SocketError)> (&QAbstractSocket::error), this, &PSKReporter::impl::handle_socket_error);
#endif

    // use this for pseudo connection with UDP, allows us to use
    // QIODevice::write() instead of QUDPSocket::writeDatagram()
    socket_->connectToHost (HOST, SERVICE_PORT, QAbstractSocket::WriteOnly);
    LOG_LOG_LOCATION (logger_, debug, "remote host: " << HOST.latin1 () << " port: " << SERVICE_PORT);

    if (!report_timer_.isActive ())
      {
        report_timer_.start ((MIN_SEND_INTERVAL + 1) * 1000);
      }
    if (!descriptor_timer_.isActive ())
      {
        descriptor_timer_.start (1 * 60 * 60 * 1000); // hourly
      }
  }

  void stop ()
  {
    if (socket_)
      {
        LOG_LOG_LOCATION (logger_, trace, "disconnecting");
        socket_->disconnectFromHost ();
      }
    descriptor_timer_.stop ();
    report_timer_.stop ();
  }

  void send_report (bool send_residue = false);
  void eclipse_load(QString filename);
  bool eclipse_active(QDateTime now = QDateTime::currentDateTime());

  bool flushing ()
  {
    bool flush =  FLUSH_INTERVAL && !(++flush_counter_ % FLUSH_INTERVAL);
    LOG_LOG_LOCATION (logger_, trace, "flush: " << flush);
    return flush;
  }

  QList<QDateTime> eclipseDates;

  logger_type mutable logger_;
  PSKReporter * self_;
  PSKReporter::Options options_;
  QSharedPointer<QAbstractSocket> socket_;
  quint32 sequence_number_;
  int send_descriptors_;

  unsigned flush_counter_;
  quint32 observation_id_;
  QString rx_call_;
  QString rx_grid_;
  QString rx_ant_;
  QString rigInformation_;
  QQueue<PSKReporterIPFIX::Spot> spots_;
  QTimer report_timer_;
  QTimer descriptor_timer_;
};
  
#include "PSKReporter.moc"

bool PSKReporter::impl::eclipse_active(QDateTime timeutc)
{
  Q_UNUSED (timeutc);
#ifdef DEBUGECLIPSE
  std::ofstream mylog("/temp/eclipse.log", std::ios_base::app);
#endif
  QDateTime const dateNow = QDateTime::currentDateTimeUtc ();
  for (auto const& check : eclipseDates)
    {
      auto const secondsDiff = qAbs (check.secsTo (dateNow));
      if (secondsDiff <= 3600 * 6)
        {
#ifdef DEBUGECLIPSE
          mylog << dateNow.toString(Qt::ISODate) << " Eclipse! " << "secondsDiff=" << secondsDiff << std::endl;
#endif
          return true;
        }
    }
#ifdef DEBUGECLIPSE
  mylog << timeutc.toString("yyyy-MM-dd HH:mm:ss") << " no eclipse" << "\n";
#endif
  return false;
}

void PSKReporter::impl::eclipse_load(QString eclipse_file)
{
  std::ifstream fs(qPrintable(eclipse_file));
#ifdef DEBUGECLIPSE
  std::ofstream mylog("/temp/eclipse.log");
  mylog << "eclipse_file=" << qPrintable(eclipse_file) << std::endl;
#endif
  std::string line;
  while (std::getline (fs, line))
    {
      if (line.size () <= 2 || line[0] == '#')
        {
          continue;
        }
      auto const eclipse = QDateTime::fromString(QString::fromStdString(line), Qt::ISODate);
      if (eclipse.isValid ())
        {
          eclipseDates.append(eclipse);
        }
#ifdef DEBUGECLIPSE
      mylog << line << std::endl;
#endif
    }
#ifdef DEBUGECLIPSE
  if (eclipse_active(QDateTime::currentDateTime().toUTC())) mylog << "Eclipse is active" << std::endl;
  else mylog << "Eclipse is not active" << std::endl;
#endif
}

void PSKReporter::impl::send_report (bool send_residue)
{
  LOG_LOG_LOCATION (logger_, trace, "sending residue: " << send_residue);
  check_connection ();
  if (!socket_ || QAbstractSocket::ConnectedState != socket_->state ())
    {
      return;
    }

  auto flush = flushing () || send_residue;
  if (!spots_.size () && !flush && !send_descriptors_)
    {
      return;
    }

  QList<PSKReporterIPFIX::Spot> spots;
  while (!spots_.isEmpty ())
    {
      spots.append (spots_.dequeue ());
    }

  bool const include_descriptors = send_descriptors_ > 0;
  if (include_descriptors)
    {
      --send_descriptors_;
    }

  auto const max_payload_bytes = options_.use_tcpip
    ? PSKReporterIPFIX::maxTcpIpfixPayloadBytes ()
    : PSKReporterIPFIX::maxUdpIpfixPayloadBytes ();
  auto const packets = PSKReporterIPFIX::buildPackets (
    {rx_call_, rx_grid_, options_.program_info, rx_ant_, rigInformation_}
    , spots
    , include_descriptors
    , sequence_number_
    , observation_id_
    , static_cast<quint32> (
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
                            QDateTime::currentDateTime ().toSecsSinceEpoch ()
#else
                            QDateTime::currentDateTime ().toMSecsSinceEpoch () / 1000
#endif
                            )
    , max_payload_bytes);

  for (auto const& packet : packets)
    {
      Q_ASSERT (packet.payload.size () <= max_payload_bytes);
      socket_->write (packet.payload);
      sequence_number_ += packet.spot_count;
      LOG_LOG_LOCATION (logger_, debug, "sent packet bytes: " << packet.payload.size () << " spots: " << packet.spot_count);
    }
}

PSKReporter::PSKReporter (Options const& options)
  : m_ {this, options}
{
  LOG_LOG_LOCATION (m_->logger_, trace, "Started for: " << qPrintable (options.program_info));
}

PSKReporter::~PSKReporter ()
{
  // m_->send_report (true);       // send any pending spots
  LOG_LOG_LOCATION (m_->logger_, trace, "Ended");
}

void PSKReporter::reconnect ()
{
  LOG_LOG_LOCATION (m_->logger_, trace, "");
  m_->reconnect ();
}

bool PSKReporter::eclipse_active(QDateTime now)
{
  return m_->eclipse_active(now);
}

void PSKReporter::setLocalStation (QString const& call, QString const& gridSquare, QString const& antenna, QString const& rigInformation)
{
  LOG_LOG_LOCATION (m_->logger_, trace, "call: " << qPrintable (call) << " grid: " << qPrintable (gridSquare) << " ant: " << qPrintable (antenna));
  m_->check_connection ();
  if (call != m_->rx_call_ || gridSquare != m_->rx_grid_ || antenna != m_->rx_ant_ || rigInformation != m_->rigInformation_)
    {
      LOG_LOG_LOCATION (m_->logger_, trace, "updating information");
      m_->rx_call_ = call;
      m_->rx_grid_ = gridSquare;
      m_->rx_ant_ = antenna;
      m_->rigInformation_ = rigInformation;
    }
}

bool PSKReporter::addRemoteStation (QString const& call, QString const& grid, Radio::Frequency freq
                                     , QString const& mode, int snr, QDateTime qSpotTime)
{
  LOG_LOG_LOCATION (m_->logger_, trace, "call: " << qPrintable (call) << " grid: " << qPrintable (grid) << " freq: " << freq << " mode: " << qPrintable (mode) << " snr: " << snr);
  m_->check_connection ();
  // remove any earlier spots of this call to reduce pskreporter load
#ifdef DEBUGPSK
  static std::fstream fs;
  if (!fs.is_open()) fs.open("/temp/psk.log", std::fstream::in | std::fstream::out | std::fstream::app);
#endif
#ifdef DEBUGPSK
  added++;
#endif

  QDateTime qdateNow = QDateTime::currentDateTime().toUTC();
  // We allow all spots through +/- 6 hours around an eclipse for the HamSCI group.
  if (!spot_cache.contains(call) || freq > 49000000 || eclipse_active(qdateNow))
    {
      m_->spots_.enqueue ({call, grid, snr, freq, mode, qSpotTime});
      spot_cache.insert(call, time(NULL));
#ifdef DEBUGPSK
      if (fs.is_open()) fs << "Adding   " << call << " freq=" << freq << " " << spot_cache[call] <<  " count=" << m_->spots_.count() << std::endl;
#endif
    }
  else if (time(NULL) - spot_cache[call] > CACHE_TIMEOUT)
    {
      m_->spots_.enqueue ({call, grid, snr, freq, mode, qSpotTime});
#ifdef DEBUGPSK
      if (fs.is_open()) fs << "Adding # " << call << spot_cache[call] << " count=" << m_->spots_.count() << std::endl;
#endif
      spot_cache[call] = time(NULL);
    }
  else
    {
#ifdef DEBUGPSK
      removed++;
      if (fs.is_open()) fs << "Removing " << call << " " << time(NULL) << " reduction=" << removed/(double)added*100 << "%" << std::endl;
#endif
    }

  bool accepted = true;
  while (m_->spots_.size () > MAX_PENDING_SPOTS)
    {
      m_->spots_.dequeue ();
      accepted = false;
      LOG_LOG_LOCATION (m_->logger_, warning, "PSKReporter pending spot queue exceeded " << MAX_PENDING_SPOTS << "; dropped oldest spot");
    }

  QMapIterator<QString, time_t> i(spot_cache);
  time_t tmptime = time(NULL);
  while(i.hasNext()) {
      i.next();
      if (tmptime - i.value() > 600) spot_cache.remove(i.key());
  }
  return accepted;
}

void PSKReporter::sendReport (bool last)
{
  LOG_LOG_LOCATION (m_->logger_, trace, "last: " << last);
  m_->check_connection ();
  if (m_->socket_ && QAbstractSocket::ConnectedState == m_->socket_->state ())
    {
      m_->send_report (true);
    }
  if (last)
    {
      m_->stop ();
    }
}
