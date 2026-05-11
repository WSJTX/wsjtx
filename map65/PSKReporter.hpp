#ifndef PSK_REPORTER_HPP_
#define PSK_REPORTER_HPP_
#include <boost/log/sources/severity_channel_logger.hpp>   
#include <boost/log/trivial.hpp>

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QQueue>
#include <QTcpSocket>
#include <QScopedPointer>
#include "Radio.hpp"
#include "../qt_helpers.hpp"
#include "commons.h"

class QString;

class PSKReporter : public QObject
{
  Q_OBJECT
  
signals:
  void dataReady(const QByteArray &payload);


public slots:
  void restartReportTimer();
  void restartDescriptorTimer();
  void init();        // Slot to initialize timers/sockets
  
    
public:
  explicit PSKReporter (QString const& myCall, QString const& myGrid, QString const& program_info);
  ~PSKReporter ();

  void setLocalStation (QString const& call, QString const& grid, QString const& antenna, QString const& rigInformation);    
  void build_preamble (QDataStream& message);
  void send_report (bool send_residue);
  void stop ();
  bool flushing();
  void writeUtfString (QDataStream& out, QString const& s);
  int num_pad_bytes (int len);
  void set_length (QDataStream& out, QByteArray& b);
  
  Q_SIGNAL void errorOccurred (QString const& reason);
  
  struct Spot
  {
    bool operator == (Spot const& rhs)
    {
      return
        call_ == rhs.call_
        && grid_ == rhs.grid_
        && mode_ == rhs.mode_
        && std::abs (Radio::FrequencyDelta (freq_ - rhs.freq_)) < 50;
    }

    QString call_;
    QString grid_;
    int snr_;
    Radio::Frequency freq_;
    QString mode_;
    QDateTime time_;
  };
  
  void addRemoteStation (QString const& call, QString const& grid, Radio::Frequency freq, QString const& mode, int snr, QDateTime spotTime);

  //
  // Flush any pending spots to PSK Reporter
  //
  void sendReport (bool last = false);

private slots:
  void handle_socket_error(QAbstractSocket::SocketError error);
  void onConnected();
  void onReadyRead();
  void sendData(const QByteArray &payload);

private:
  #ifdef Q_OS_MACOS
    // Disable Boost.Log on macOS (crashes during default sink init)
    #define M65_LOG(sev) if (true) {} else std::cerr
#else
    using logger_type = boost::log::sources::severity_channel_logger_mt<
        boost::log::trivial::severity_level
    >;

    #define M65_LOG(sev) BOOST_LOG_SEV(logger_, sev)
#endif
#ifndef Q_OS_MACOS
    logger_type logger_;
#endif
  QString myCall;
  QString myGrid;  
  quint32 sequence_number_; 
  int send_descriptors_;
  int send_receiver_data_;
  unsigned flush_counter_ ;
  QString prog_id_;
  unsigned observation_id_;
  QTimer *report_timer_;
  QTimer *descriptor_timer_;
  QScopedPointer<QTcpSocket> socket; // Or: std::unique_ptr<QSslSocket  
  int dns_lookup_id_;
  QByteArray payload_;
  QString rx_call_;
  QString rx_grid_;
  QString rx_ant_;
  QString rigInformation_;
  QByteArray tx_data_;
  QByteArray tx_residue_;  
  QQueue<Spot> spots_;

  // Copy constructor and assignment operator disabled
  Q_DISABLE_COPY(PSKReporter)
};

#endif
