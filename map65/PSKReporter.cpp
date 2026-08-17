#include "PSKReporter.hpp"
#include <QSettings>
#include "SettingsGroup.hpp"
#include "mainwindow.h"
#include "qt_helpers.hpp"
#include "../revision_utils.hpp"

// Interface for posting spots to PSK Reporter web site
// Implemented by Edson Pereira PY2SDR
// Updated by Bill Somerville, G4WJS
//
// Reports will be sent in batch mode every 5 minutes.

#include <fstream>
#include <iostream>
#include <cmath>
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QSharedPointer>
#include <QTcpSocket>
#include <QHostInfo>
#include <QQueue>
#include <QByteArray>
#include <QDataStream>
#include <QTimer>
#include <QDir>
#include <QSettings>
#include <QDebug>
#include <QMetaType>
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
#include <QRandomGenerator>
#endif

#include "devsetup.h"

//#define DEBUGECLIPSE 0
# define DEBUGPSK 1;

namespace
{
  QLatin1String HOST {"report.pskreporter.info"};
  //QLatin1String HOST {"127.0.0.1"};
  quint16 SERVICE_PORT {4739};  //this is normal user port
  //quint16 SERVICE_PORT {14739}; //this is test port
  int MIN_SEND_INTERVAL {120}; // in seconds
  int FLUSH_INTERVAL {MIN_SEND_INTERVAL + 5}; // in send intervals
  bool ALIGNMENT_PADDING {true};
  int MIN_PAYLOAD_LENGTH {508};
  int MAX_PAYLOAD_LENGTH {10000};
  int CACHE_TIMEOUT {300}; // default to 5 minutes for repeating spots
  QMap<QString, time_t> spot_cache;
}

static int added;
static int removed;

PSKReporter::PSKReporter(QString const& myCall, QString const& myGrid, QString const& program_info)  
    : 
    myCall {myCall},
     myGrid {myGrid},
     sequence_number_ {0u},
     send_descriptors_ {1},
     send_receiver_data_ {1},
     flush_counter_ {0u},
     prog_id_ {program_info}
  {
        
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    observation_id_ = qrand();
#else
    observation_id_ = QRandomGenerator::global ()->generate ();
#endif

    qDebug() << "PSKReporter Current thread:" << QThread::currentThread();
  }
    
  void PSKReporter::handle_socket_error (QAbstractSocket::SocketError e)
  {
    //LOG_LOG_LOCATION (logger_, warning, "PSKReporter socket error: " << socket->errorString ());
    switch (e)
      {
      case QAbstractSocket::RemoteHostClosedError:
        socket->disconnectFromHost ();
        break;

      case QAbstractSocket::TemporaryError:
        break;

      default:
        spots_.clear ();
        Q_EMIT this->errorOccurred (socket->errorString ());
        break;
      }
  }

  void PSKReporter::stop ()
  {
    if (socket)
      {
        //LOG_LOG_LOCATION (logger_, trace, "PSKReporter disconnecting");
        socket->disconnectFromHost ();
      }
    this->descriptor_timer_->stop ();
    this->report_timer_->stop ();
  }


  bool PSKReporter::flushing ()
  {
    bool flush =  FLUSH_INTERVAL && !(++flush_counter_ % FLUSH_INTERVAL);
    //LOG_LOG_LOCATION (logger_, trace, "PSKReporter flush: " << flush);
    return flush;
  }

  void PSKReporter::writeUtfString (QDataStream& out, QString const& s)
  {
    auto const& utf = s.toUtf8 ().left (254);
    out << quint8 (utf.size ());
    out.writeRawData (utf, utf.size ());
  }

  int PSKReporter::num_pad_bytes (int len)
  {
    return ALIGNMENT_PADDING ? (4 - len % 4) % 4 : 0;
  }

  void PSKReporter::set_length (QDataStream& out, QByteArray& b)
  {
    // pad with nulls modulo 4
    auto pad_len = num_pad_bytes (b.size ());
    out.writeRawData (QByteArray {pad_len, '\0'}.constData (), pad_len);
    auto pos = out.device ()->pos ();
    out.device ()->seek (sizeof (quint16));
    // insert length
    out << static_cast<quint16> (b.size ());
    out.device ()->seek (pos);
  }

  void PSKReporter::restartReportTimer() {
    report_timer_->stop();
    report_timer_->start((MIN_SEND_INTERVAL+1) * 1000);
    qDebug() << "PSKReporter Current thread for restartReportTimer:" << QThread::currentThread();
  }
  
  void PSKReporter::restartDescriptorTimer() {
    descriptor_timer_->stop();
    descriptor_timer_->start(60 * 60 * 1000);
    qDebug() << "PSKReporter Current thread for restartDescriptorTimer:" << QThread::currentThread();
  }

void PSKReporter::init() {
    report_timer_ = new QTimer(this);
    descriptor_timer_ = new QTimer(this);
    connect(report_timer_, &QTimer::timeout,
        this, &PSKReporter::restartReportTimer);
	  connect(descriptor_timer_, &QTimer::timeout,
        this, &PSKReporter::restartDescriptorTimer);

    report_timer_->start((MIN_SEND_INTERVAL + 1) * 1000);
    descriptor_timer_->start(60 * 60 * 1000); // 1 hour
    qDebug() << "PSKReporter::init running on thread: " << QThread::currentThread();
        
    socket.reset(new QTcpSocket(this));
    
    // Connect signals before connecting
    connect(socket.data(), &QAbstractSocket::connected, this, &PSKReporter::onConnected);
    connect(socket.data(), &QAbstractSocket::readyRead, this, &PSKReporter::onReadyRead); 
    connect(this, &PSKReporter::dataReady, this, &PSKReporter::sendData);   
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(socket.data(), &QAbstractSocket::errorOccurred, this, &PSKReporter::handle_socket_error);  
#else   
    connect(socket.data(), static_cast<void(QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error),
        this, &PSKReporter::handle_socket_error);  
#endif
    // This timer sets the interval to check for spots to send.
    connect (report_timer_, &QTimer::timeout, [this] () {this->send_report (true);});

    connect (descriptor_timer_, &QTimer::timeout, [this] () {this->send_descriptors_ = 1;
           this->send_receiver_data_ = 1;
     });         
         
    // Start connection (non-blocking
    setLocalStation(myCall, myGrid,"N/A","N/A (MAP65)");
    socket->connectToHost(HOST, SERVICE_PORT);
            
}   
 
  void PSKReporter::onConnected() {
        
    qDebug() << "PSKReporter onConnected Connected securely!";
    qDebug() << "PSKReporter::onConnected payload: " << payload_;// << request;
    socket->write(payload_); 
  }
  
  void PSKReporter::onReadyRead() {
    QByteArray data = socket->readAll();
    // Parse headers, then handle chunks
    while (!data.isEmpty()) {
        int pos = data.indexOf("\r\n");
        bool ok;
        int size = data.left(pos).trimmed().toInt(&ok, 16);
        if (!ok || size == 0) break;
        data = data.mid(pos + 2);
        qDebug() << "Chunk:" << data.left(size);
        data = data.mid(size + 2); // skip \r\n
    }
      qDebug() << "PSKReporter Response:" <<   data;
}   
    
  void PSKReporter::sendData(const QByteArray &payload1) {
    if (socket->state() == QAbstractSocket::ConnectedState) {
      socket->write(payload1);
      qDebug() << "PSKReporter::sendData payload: " << payload1;// << request;
      }
    else {
    socket->connectToHost(HOST, SERVICE_PORT);
    // Data will be sent in onConnected()
    }
  }

void PSKReporter::build_preamble (QDataStream& message)
{
  // Message Header
  message
    << quint16 (10u)          // Version Number
    << quint16 (0u)           // Length (place-holder filled in later)
    << quint32 (0u)           // Export Time (place-holder filled in later)
    << ++sequence_number_     // Sequence Number
    << observation_id_;       // Observation Domain ID
  //LOG_LOG_LOCATION (logger_, trace, "PSKReporter #: " << sequence_number_);

  if (send_descriptors_)
    {
      --send_descriptors_;
      {
        // Sender Information descriptor
        QByteArray descriptor;
        QDataStream out {&descriptor, QIODevice::WriteOnly};
        out
          << quint16 (2u)           // Template Set ID
          << quint16 (0u)           // Length (place-holder)
          << quint16 (0x50e3)       // Link ID
          << quint16 (7u)           // Field Count
          << quint16 (0x8000 + 1u)  // Option 1 Information Element ID (senderCallsign)
          << quint16 (0xffff)       // Option 1 Field Length (variable)
          << quint32 (30351u)       // Option 1 Enterprise Number
          << quint16 (0x8000 + 5u)  // Option 2 Information Element ID (frequency)
          << quint16 (5u)           // Option 2 Field Length
          << quint32 (30351u)       // Option 2 Enterprise Number
          << quint16 (0x8000 + 6u)  // Option 3 Information Element ID (sNR)
          << quint16 (1u)           // Option 3 Field Length
          << quint32 (30351u)       // Option 3 Enterprise Number
          << quint16 (0x8000 + 10u) // Option 4 Information Element ID (mode)
          << quint16 (0xffff)       // Option 4 Field Length (variable)
          << quint32 (30351u)       // Option 4 Enterprise Number
          << quint16 (0x8000 + 3u)  // Option 5 Information Element ID (senderLocator)
          << quint16 (0xffff)       // Option 5 Field Length (variable)
          << quint32 (30351u)       // Option 5 Enterprise Number
          << quint16 (0x8000 + 11u) // Option 6 Information Element ID (informationSource)
          << quint16 (1u)           // Option 6 Field Length
          << quint32 (30351u)       // Option 6 Enterprise Number
          << quint16 (150u)         // Option 7 Information Element ID (dateTimeSeconds)
          << quint16 (4u);          // Option 7 Field Length
        // insert Length and move to payload
        set_length (out, descriptor);
        message.writeRawData (descriptor.constData (), descriptor.size ());
      }
      {
        // Receiver Information descriptor
        QByteArray descriptor;
        QDataStream out {&descriptor, QIODevice::WriteOnly};
        out
          << quint16 (3u)          // Options Template Set ID
          << quint16 (0u)          // Length (place-holder)
          << quint16 (0x50e2)      // Link ID
          << quint16 (5u)          // Field Count
          << quint16 (0u)          // Scope Field Count
          << quint16 (0x8000 + 2u) // Option 1 Information Element ID (receiverCallsign)
          << quint16 (0xffff)      // Option 1 Field Length (variable)
          << quint32 (30351u)      // Option 1 Enterprise Number
          << quint16 (0x8000 + 4u) // Option 2 Information Element ID (receiverLocator)
          << quint16 (0xffff)      // Option 2 Field Length (variable)
          << quint32 (30351u)      // Option 2 Enterprise Number
          << quint16 (0x8000 + 8u) // Option 3 Information Element ID (decodingSoftware)
          << quint16 (0xffff)      // Option 3 Field Length (variable)
          << quint32 (30351u)      // Option 3 Enterprise Number
          << quint16 (0x8000 + 9u) // Option 4 Information Element ID (antennaInformation)
          << quint16 (0xffff)      // Option 4 Field Length (variable)
          << quint32 (30351u)      // Option 5 Enterprise Number
          << quint16 (0x8000 + 13u)// Option 5 Information Element ID (rigInformation)
          << quint16 (0xffff)      // Option 5 Field Length (variable)
          << quint32 (30351u);     // Option 4 Enterprise Number
        // insert Length
        set_length (out, descriptor);
        message.writeRawData (descriptor.constData (), descriptor.size ());
        //LOG_LOG_LOCATION (logger_, debug, "PSKReporter sent descriptors");
      }
    }

  // if (send_receiver_data_)
  {
    // --send_receiver_data_;

    // Receiver information
    QByteArray data;
    QDataStream out {&data, QIODevice::WriteOnly};

    // Set Header
    out
      << quint16 (0x50e2)     // Template ID
      << quint16 (0u);        // Length (place-holder)

    // Set data
    writeUtfString (out, rx_call_);
    writeUtfString (out, rx_grid_);
    writeUtfString (out, prog_id_);
    writeUtfString (out, rx_ant_);
    writeUtfString (out, rigInformation_);

    // insert Length and move to payload
    set_length (out, data);
    message.writeRawData (data.constData (), data.size ());
    //LOG_LOG_LOCATION (logger_, debug, "PSKReporter sent local information");
  }
}

void PSKReporter::send_report (bool send_residue)
{
//  LOG_LOG_LOCATION (logger_, trace, "PSKReporter sending residue: " << send_residue);
  qDebug() << "PSKReporter send_report QAbstractSocket::ConnectedState is: " << socket->state (); 

  QDataStream message {&payload_, QIODevice::WriteOnly | QIODevice::Append};
  QDataStream tx_out {&tx_data_, QIODevice::WriteOnly | QIODevice::Append};

  if (!payload_.size ())
    {
      // Build header, optional descriptors, and receiver information
      build_preamble (message);
    }

  auto flush = flushing () || send_residue;
  while (spots_.size () || flush)
    {
      if (!payload_.size ())
        {
          // Build header, optional descriptors, and receiver information
          build_preamble (message);
        }

      if (!tx_data_.size () && (spots_.size () || tx_residue_.size ()))
        {
          // Set Header
          tx_out
            << quint16 (0x50e3)     // Template ID
            << quint16 (0u);        // Length (place-holder)
        }

      // insert any residue
      if (tx_residue_.size ())
        {
          tx_out.writeRawData (tx_residue_.constData (), tx_residue_.size ());
        //  LOG_LOG_LOCATION (logger_, debug, "PSKReporter sent residue");
          tx_residue_.clear ();
        }

      //LOG_LOG_LOCATION (logger_, debug, "PSKReporter pending spots: " << spots_.size ());
      while (spots_.size () || flush)
        {
          auto tx_data_size = tx_data_.size ();
          if (spots_.size ())
            {
              auto const& spot = spots_.dequeue ();

              // Sender information
              writeUtfString (tx_out, spot.call_);
              uint8_t data[5];
              long long int i64 = spot.freq_;
              data[0] = ( i64 & 0xff);
              data[1] = ((i64 >>  8) & 0xff);
              data[2] = ((i64 >> 16) & 0xff);
              data[3] = ((i64 >> 24) & 0xff);
              data[4] = ((i64 >> 32) & 0xff);
              tx_out // BigEndian
                << static_cast<uint8_t> (data[4])
                << static_cast<uint8_t> (data[3])
                << static_cast<uint8_t> (data[2])
                << static_cast<uint8_t> (data[1])
                << static_cast<uint8_t> (data[0])
                << static_cast<qint8> (spot.snr_);
              writeUtfString (tx_out, spot.mode_);
              writeUtfString (tx_out, spot.grid_);
              tx_out
                << quint8 (1u)          // REPORTER_SOURCE_AUTOMATIC
                << static_cast<quint32> (
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
                                         spot.time_.toSecsSinceEpoch ()
#else
                                         spot.time_.toMSecsSinceEpoch () / 1000
#endif
                                         );
            }

          auto len = payload_.size () + tx_data_.size ();
          len += num_pad_bytes (tx_data_.size ());
          len += num_pad_bytes (len);
          if (len > MAX_PAYLOAD_LENGTH // our upper datagram size limit
              || (!spots_.size () && len > MIN_PAYLOAD_LENGTH) // spots drained and above lower datagram size limit
              || (flush && !spots_.size ())) // send what we have, possibly no spots
            {
              if (tx_data_.size ())
                {
                  if (len <= MAX_PAYLOAD_LENGTH)
                    {
                      tx_data_size = tx_data_.size ();
                    }
                  QByteArray tx {tx_data_.left (tx_data_size)};
                  QDataStream out {&tx, QIODevice::WriteOnly | QIODevice::Append};
                  // insert Length
                  set_length (out, tx);
                  message.writeRawData (tx.constData (), tx.size ());
                }

              // insert Length and Export Time
              set_length (message, payload_);
              message.device ()->seek (2 * sizeof (quint16));
              message << static_cast<quint32> (
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
                                               QDateTime::currentDateTime ().toSecsSinceEpoch ()
#else
                                               QDateTime::currentDateTime ().toMSecsSinceEpoch () / 1000
#endif
                                               );

              // Send data to PSK Reporter site
              //socket->write (payload_); // TODO: handle errors
              emit dataReady(payload_);
            //  LOG_LOG_LOCATION (logger_, debug, "PSKReporter sent spots");
              flush = false;    // break loop
              message.device ()->seek (0u);
              payload_.clear ();  // Fresh message
              // Save unsent spots
              tx_residue_ = tx_data_.right (tx_data_.size () - tx_data_size);
              tx_out.device ()->seek (0u);
              tx_data_.clear ();
              break;
            }
        }
      //LOG_LOG_LOCATION (logger_, debug, "PSKReporter remaining spots: " << spots_.size ());
      QThread::yieldCurrentThread(); // In long loops   
    }
    qDebug() << "PSKReporter current thread for send_report:" << QThread::currentThread();
}

void PSKReporter::setLocalStation (QString const& call, QString const& gridSquare, QString const& antenna, QString const& rigInformation)
{
  //LOG_LOG_LOCATION (logger_, trace, "PSKReporter call: " << call << " grid: " << gridSquare << " ant: " << antenna);
  qDebug() << "PSKReporter setLocalStation QAbstractSocket::ConnectedState is: " << socket->state (); 
  qDebug() << "PSKReporter Connected to peer IP:" << socket->peerAddress().toString(); 
  qDebug() << "PSKReporter Current thread for setLocalStation:" << QThread::currentThread();  
  
  if (call != rx_call_ || gridSquare != rx_grid_ || antenna != rx_ant_)
    {
      //LOG_LOG_LOCATION (logger_, trace, "PSKReporter updating information");
      send_receiver_data_ = 1;
      rx_call_ = call;
      rx_grid_ = gridSquare;
      rx_ant_ = antenna;
      rigInformation_ = rigInformation;
    }
}

void PSKReporter::addRemoteStation (QString const& call, QString const& grid, Radio::Frequency freq
                                     , QString const& mode, int snr, QDateTime qSpotTime)
{
  //LOG_LOG_LOCATION (logger_, trace, "PSKReporter call: " << call << " grid: " << grid << " freq: " << freq << " mode: " << mode << " snr: " << snr);
  qDebug() << "PSKReporter addRemoteStation QAbstractSocket::ConnectedState is: " << socket->state ();
  qDebug() << "PSKReporter Connected to peer IP:" << socket->peerAddress().toString();    
  qDebug() << "PSKReporter Current thread for addRemoteStation:" << QThread::currentThread();  
  if (socket && socket->isValid ())
    {
      // remove any earlier spots of this call to reduce pskreporter load
#ifdef DEBUGPSK
      static std::fstream fs;
      if (!fs.is_open()) fs.open("/temp/psk.log", std::fstream::in | std::fstream::out | std::fstream::app);
#endif
      added++;

      QDateTime qdateNow = QDateTime::currentDateTime().toUTC();
      // we allow all spots through +/- 6 hours around an eclipse for the HamSCI group
      if (!spot_cache.contains(call) || freq > 49000000) // then it's a new spot
      {
        spots_.enqueue ({call, grid, snr, freq, mode, qSpotTime});
        spot_cache.insert(call, time(NULL));
#ifdef DEBUGPSK
        if (fs.is_open()) fs << "PSKReporter Adding   " << call << " freq=" << freq << " " << spot_cache[call] <<  " count=" << spots_.count() << std::endl;
#endif
      }
      else if (time(NULL) - spot_cache[call] > CACHE_TIMEOUT) // then the cache has expired  
      {
        spots_.enqueue ({call, grid, snr, freq, mode, qSpotTime});
#ifdef DEBUGPSK
        if (fs.is_open()) fs << "PSKReporter Adding # " << call << spot_cache[call] << " count=" << spots_.count() << std::endl;
#endif
        spot_cache[call] = time(NULL);
      }
      else
      {
        removed++;
#ifdef DEBUGPSK
        if (fs.is_open()) fs << "Removing " << call << " " << time(NULL) << " reduction=" << removed/(double)added*100 << "%" << std::endl;
#endif
      }
      // remove cached items over 10 minutes old to save a little memory
      QMapIterator<QString, time_t> i(spot_cache);
      time_t tmptime = time(NULL);
      while(i.hasNext()) {
          i.next();
          if (tmptime - i.value() > 600) spot_cache.remove(i.key());
          QThread::yieldCurrentThread(); // In long loops   
      }
    }
}

void PSKReporter::sendReport (bool last)
{
  //LOG_LOG_LOCATION (logger_, trace, "PSKReporter last: " << last);
  qDebug() << "in PSKReporter sendReport QAbstractSocket::ConnectedState is: " << socket->state ();   
  if (socket && QAbstractSocket::ConnectedState == socket->state ())
    {
      send_report (true);
    }
  if (last)
    {
      stop ();
    }
}

PSKReporter::~PSKReporter() = default;
