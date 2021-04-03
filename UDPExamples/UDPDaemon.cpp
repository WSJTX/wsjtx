//
// UDPDaemon - an example console application that utilizes the WSJT-X
//             messaging facility
//
// This application is  only provided as a  simple console application
// example to  demonstrate the  WSJT-X messaging facility.   It allows
// the user to  set the server details either as  a unicast UDP server
// or,  if a  multicast  group  address is  provided,  as a  multicast
// server.   The benefit  of  the multicast  server  is that  multiple
// servers can be  active at once each receiving  all WSJT-X broadcast
// messages and each able to respond to individual WSJT_X clients.  To
// utilize the  multicast group features  each WSJT-X client  must set
// the  same multicast  group address  as the  UDP server  address for
// example 239.255.0.0 for a site local multicast group.
//
//

#include <iostream>
#include <exception>
#include <locale>
#include <cstdlib>

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QString>
#include <QStringList>
#include <QNetworkInterface>
#include <QDateTime>
#include <QTime>
#include <QHash>
#include <QDebug>

#include "MessageServer.hpp"
#include "Radio.hpp"

#include "qt_helpers.hpp"

using port_type = MessageServer::port_type;
using Frequency = MessageServer::Frequency;

class Client
  : public QObject
{
  Q_OBJECT

  using ClientKey = MessageServer::ClientKey;

public:
  explicit Client (ClientKey const& key, QObject * parent = nullptr)
    : QObject {parent}
    , key_ {key}
    , dial_frequency_ {0u}
  {
  }

  Q_SLOT void update_status (ClientKey const& key, Frequency f, QString const& mode, QString const& /*dx_call*/
                             , QString const& /*report*/, QString const& /*tx_mode*/, bool /*tx_enabled*/
                             , bool /*transmitting*/, bool /*decoding*/, qint32 /*rx_df*/, qint32 /*tx_df*/
                             , QString const& /*de_call*/, QString const& /*de_grid*/, QString const& /*dx_grid*/
                             , bool /* watchdog_timeout */, QString const& sub_mode, bool /*fast_mode*/
                             , quint8 /*special_op_mode*/, quint32 /*frequency_tolerance*/, quint32 /*tr_period*/
                             , QString const& /*configuration_name*/, QString const& /*tx_message*/)
  {
    if (key == key_)
      {
        if (f != dial_frequency_)
          {
            std::cout << QString {"%1(%2): "}.arg (key_.second).arg (key_.first.toString ()).toStdString ()
                      << QString {"Dial frequency changed to %1"}.arg (f).toStdString () << std::endl;
            dial_frequency_ = f;
          }
        if (mode + sub_mode != mode_)
          {
            std::cout << QString {"%1(%2): "}.arg (key_.second).arg (key_.first.toString ()).toStdString ()
                      << QString {"Mode changed to %1"}.arg (mode + sub_mode).toStdString () << std::endl;
            mode_ = mode + sub_mode;
          }
      }
  }

  Q_SLOT void decode_added (bool is_new, ClientKey const& key, QTime time, qint32 snr
                            , float delta_time, quint32 delta_frequency, QString const& mode
                            , QString const& message, bool low_confidence, bool off_air)
  {
    if (key == key_)
      {
        qDebug () << "new:" << is_new << "t:" << time << "snr:" << snr
                  << "Dt:" << delta_time << "Df:" << delta_frequency
                  << "mode:" << mode << "Confidence:" << (low_confidence ? "low" : "high")
                  << "On air:" << !off_air;
        std::cout << QString {"%1(%2): "}.arg (key_.second).arg (key_.first.toString ()).toStdString ()
                  << QString {"Decoded %1"}.arg (message).toStdString () << std::endl;
      }
  }

  Q_SLOT void beacon_spot_added (bool is_new, ClientKey const& key, QTime time, qint32 snr
      , float delta_time, Frequency delta_frequency, qint32 drift, QString const& callsign
                                 , QString const& grid, qint32 power, bool off_air)
  {
    if (key == key_)
      {
        qDebug () << "new:" << is_new << "t:" << time << "snr:" << snr
                  << "Dt:" << delta_time << "Df:" << delta_frequency
                  << "drift:" << drift;
        std::cout << QString {"%1(%2): "}.arg (key_.second).arg (key_.first.toString ()).toStdString ()
                  << QString {"WSPR decode %1 grid %2 power: %3"}
                       .arg (callsign).arg (grid).arg (power).toStdString ()
                  << "On air:" << !off_air << std::endl;
      }
  }

  Q_SLOT void qso_logged (ClientKey const& key, QDateTime time_off, QString const& dx_call, QString const& dx_grid
                          , Frequency dial_frequency, QString const& mode, QString const& report_sent
                          , QString const& report_received, QString const& tx_power
                          , QString const& comments, QString const& name, QDateTime time_on
                          , QString const& operator_call, QString const& my_call, QString const& my_grid
                          , QString const& exchange_sent, QString const& exchange_rcvd, QString const& prop_mode)
  {
      if (key == key_)
      {
        qDebug () << "time_on:" << time_on << "time_off:" << time_off << "dx_call:"
                  << dx_call << "grid:" << dx_grid
                  << "freq:" << dial_frequency << "mode:" << mode << "rpt_sent:" << report_sent
                  << "rpt_rcvd:" << report_received << "Tx_pwr:" << tx_power << "comments:" << comments
                  << "name:" << name << "operator_call:" << operator_call << "my_call:" << my_call
                  << "my_grid:" << my_grid << "exchange_sent:" << exchange_sent
                  << "exchange_rcvd:" << exchange_rcvd << "prop_mode:" << prop_mode;
        std::cout << QByteArray {80, '-'}.data () << '\n';
        std::cout << QString {"%1(%2): "}.arg (key_.second).arg (key_.first.toString ()).toStdString ()
                  << QString {"Logged %1 grid: %2 power: %3 sent: %4 recd: %5 freq: %6 time_off: %7 op: %8 my_call: %9 my_grid: %10 exchange_sent: %11 exchange_rcvd: %12 comments: %13 prop_mode: %14"}
                       .arg (dx_call).arg (dx_grid).arg (tx_power)
                       .arg (report_sent).arg (report_received)
                       .arg (dial_frequency).arg (time_off.toString("yyyy-MM-dd hh:mm:ss.z")).arg (operator_call)
                       .arg (my_call).arg (my_grid).arg (exchange_sent).arg (exchange_rcvd)
                       .arg (comments).arg (prop_mode).toStdString ()
                  << std::endl;
      }
  }

  Q_SLOT void logged_ADIF (ClientKey const& key, QByteArray const& ADIF)
  {
      if (key == key_)
      {
        qDebug () << "ADIF:" << ADIF;
        std::cout << QByteArray {80, '-'}.data () << '\n';
        std::cout << ADIF.data () << std::endl;
      }
  }

private:
  ClientKey key_;
  Frequency dial_frequency_;
  QString mode_;
};

class Server
  : public QObject
{
  Q_OBJECT

  using ClientKey = MessageServer::ClientKey;

public:
  Server (port_type port, QHostAddress const& multicast_group, QStringList const& network_interface_names)
    : server_ {new MessageServer {this}}
  {
    // connect up server
    connect (server_, &MessageServer::error, [] (QString const& message) {
        std::cerr << tr ("Network Error: %1").arg ( message).toStdString () << std::endl;
      });
    connect (server_, &MessageServer::client_opened, this, &Server::add_client);
    connect (server_, &MessageServer::client_closed, this, &Server::remove_client);

#if QT_VERSION >= QT_VERSION_CHECK (5, 14, 0)
    server_->start (port, multicast_group, QSet<QString> {network_interface_names.begin (), network_interface_names.end ()});
#else
    server_->start (port, multicast_group, network_interface_names.toSet ());
#endif
  }

private:
  void add_client (ClientKey const& key, QString const& version, QString const& revision)
  {
    auto client = new Client {key};
    connect (server_, &MessageServer::status_update, client, &Client::update_status);
    connect (server_, &MessageServer::decode, client, &Client::decode_added);
    connect (server_, &MessageServer::WSPR_decode, client, &Client::beacon_spot_added);
    connect (server_, &MessageServer::qso_logged, client, &Client::qso_logged);
    connect (server_, &MessageServer::logged_ADIF, client, &Client::logged_ADIF);
    clients_[key] = client;
    server_->replay (key);
    std::cout << "Discovered WSJT-X instance: " << key.second.toStdString ()
              << '(' << key.first.toString ().toStdString () << ')';
    if (version.size ())
      {
        std::cout << " v" << version.toStdString ();
      }
    if (revision.size ())
      {
        std::cout << " (" << revision.toStdString () << ")";
      }
    std::cout << std::endl;
  }

  void remove_client (ClientKey const& key)
  {
    auto iter = clients_.find (key);
    if (iter != std::end (clients_))
      {
        clients_.erase (iter);
        (*iter)->deleteLater ();
      }
    std::cout << "Removed WSJT-X instance: " << key.second.toStdString ()
              << '(' << key.first.toString ().toStdString () << ')' << std::endl;
  }

  MessageServer * server_;

  // maps client key to clients
  QHash<ClientKey, Client *> clients_;
};

void list_interfaces ()
{
  for (auto const& net_if : QNetworkInterface::allInterfaces ())
    {
      if (net_if.flags () & QNetworkInterface::IsUp)
        {
          std::cout << net_if.humanReadableName ().toStdString () << ":\n"
            "  id: " << net_if.name ().toStdString () << " (" << net_if.index () << ")\n"
            "  addr: " << net_if.hardwareAddress ().toStdString () << "\n"
            "  flags: ";
          if (net_if.flags () & QNetworkInterface::IsRunning)
            {
              std::cout << "Running ";
            }
          if (net_if.flags () & QNetworkInterface::CanBroadcast)
            {
              std::cout << "Broadcast ";
            }
          if (net_if.flags () & QNetworkInterface::CanMulticast)
            {
              std::cout << "Multicast ";
            }
          if (net_if.flags () & QNetworkInterface::IsLoopBack)
            {
              std::cout << "Loop-back ";
            }
          std::cout << "\n  addresses:\n";
          for (auto const& ae : net_if.addressEntries ())
            {
              std::cout << "    " << ae.ip ().toString ().toStdString () << '\n';
            }
          std::cout << '\n';
        }
    }
}

#include "UDPDaemon.moc"

int main (int argc, char * argv[])
{
  QCoreApplication app {argc, argv};
  try
    {
      // ensure number forms are in consistent format, do this after
      // instantiating QApplication so that GUI has correct l18n
      std::locale::global (std::locale::classic ());

      app.setApplicationName ("WSJT-X UDP Message Server Daemon");
      app.setApplicationVersion ("1.0");

      QCommandLineParser parser;
      parser.setApplicationDescription ("\nWSJT-X UDP Message Server Daemon.");
      auto help_option = parser.addHelpOption ();
      auto version_option = parser.addVersionOption ();

      QCommandLineOption list_option (QStringList {"l", "list-interfaces"},
                                      app.translate ("UDPDaemon",
                                                     "Print the available network interfaces."));
      parser.addOption (list_option);

      QCommandLineOption port_option (QStringList {"p", "port"},
                                      app.translate ("UDPDaemon",
                                                     "Where <PORT> is the UDP service port number to listen on.\n"
                                                     "The default service port is 2237."),
                                      app.translate ("UDPDaemon", "PORT"),
                                      "2237");
      parser.addOption (port_option);

      QCommandLineOption multicast_addr_option (QStringList {"g", "multicast-group"},
                                                app.translate ("UDPDaemon",
                                                               "Where <GROUP> is the multicast group to join.\n"
                                                               "The default is a unicast server listening on all interfaces."),
                                                app.translate ("UDPDaemon", "GROUP"));
      parser.addOption (multicast_addr_option);

      QCommandLineOption network_interface_option (QStringList {"i", "network-interface"},
                                                   app.translate ("UDPDaemon",
                                                                  "Where <INTERFACE> is the network interface name to join on.\n"
                                                                  "This option can be passed more than once to specify multiple network interfaces\n"
                                                                  "The default is use just the loop back interface."),
                                                   app.translate ("UDPDaemon", "INTERFACE"));
      parser.addOption (network_interface_option);

      parser.process (app);

      if (parser.isSet (list_option))
        {
          list_interfaces ();
          return EXIT_SUCCESS;
        }

      Server server {static_cast<port_type> (parser.value (port_option).toUInt ())
                     , QHostAddress {parser.value (multicast_addr_option).trimmed ()}
                     , parser.values (network_interface_option)};

      return app.exec ();
    }
  catch (std::exception const & e)
    {
      std::cerr << "Error: " << e.what () << '\n';
    }
  catch (...)
    {
      std::cerr << "Unexpected error\n";
    }
  return -1;
}
