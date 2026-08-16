// inhibit-agent — CLI (scripting). Requires serial port + dest addr.
//
//   inhibit-agent --port /dev/ttyUSB0 --addr 127.0.0.1:22372
//   inhibit-agent COM7 192.168.1.40:22372
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "agent.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QTextStream>

#include <csignal>

#if defined (Q_OS_WIN)
# include <windows.h>
#endif

namespace {

QCoreApplication * g_app = nullptr;

void request_quit (int)
{
  if (g_app)
    {
      QCoreApplication::quit ();
    }
}

#if defined (Q_OS_WIN)
BOOL WINAPI console_ctrl (DWORD)
{
  request_quit (0);
  return TRUE;
}
#endif

} // namespace

int main (int argc, char * argv[])
{
  QCoreApplication app (argc, argv);
  g_app = &app;
  QCoreApplication::setApplicationName (QStringLiteral ("inhibit-agent"));
  QCoreApplication::setApplicationVersion (QStringLiteral ("1.0"));

  QCommandLineParser parser;
  parser.setApplicationDescription (
      QStringLiteral (
          "SSB/CW KEY (USB-serial CTS) -> TX Inhibit hold.\n"
          "CLI: pass the serial port and dest host:port.\n"
          "Same hang policy as inhibit-test (CW hang; SSB hang 0)."));
  parser.addHelpOption ();
  parser.addVersionOption ();
  parser.addPositionalArgument (
      QStringLiteral ("port"),
      QStringLiteral ("USB-serial device (COM7 or /dev/ttyUSB0)"),
      QStringLiteral ("[port]"));
  parser.addPositionalArgument (
      QStringLiteral ("addr"),
      QStringLiteral ("Gate address host:port"),
      QStringLiteral ("[addr]"));
  QCommandLineOption portOpt {
    QStringList () << "p" << "port",
    QStringLiteral ("USB-serial device (CTS = KEY)"),
    QStringLiteral ("device")};
  QCommandLineOption addrOpt {
    QStringList () << "a" << "addr",
    QStringLiteral ("WSJT-X gate address (host:port)"),
    QStringLiteral ("host:port")};
  QCommandLineOption invertOpt {
    QStringList () << "invert",
    QStringLiteral ("Invert CTS polarity")};
  QCommandLineOption listOpt {
    QStringList () << "list-ports",
    QStringLiteral ("List serial ports and exit")};
  QCommandLineOption ttlOpt {
    QStringList () << "t" << "ttl-ms",
    QStringLiteral ("hold_timeout_ms on hold/keepalive (default 600)"),
    QStringLiteral ("ms"),
    QStringLiteral ("600")};
  parser.addOption (portOpt);
  parser.addOption (addrOpt);
  parser.addOption (invertOpt);
  parser.addOption (listOpt);
  parser.addOption (ttlOpt);
  parser.process (app);

  QTextStream out (stdout);
  QTextStream err (stderr);

  if (parser.isSet (listOpt))
    {
      out << InhibitAgent::list_serial_ports () << '\n';
      return 0;
    }

  QStringList pos = parser.positionalArguments ();
  QString port = parser.value (portOpt);
  QString addr = parser.value (addrOpt);
  if (port.isEmpty () && pos.size () >= 1)
    {
      port = pos[0];
    }
  if (addr.isEmpty () && pos.size () >= 2)
    {
      addr = pos[1];
    }
  else if (addr.isEmpty () && pos.size () == 1 && !parser.isSet (portOpt))
    {
      // Single positional after --port DEVICE is the addr.
    }
  if (addr.isEmpty () && pos.size () == 1 && parser.isSet (portOpt))
    {
      addr = pos[0];
    }

  if (port.isEmpty () || addr.isEmpty ())
    {
      err << "inhibit-agent: CLI requires a USB-serial port and dest addr.\n"
          << "  inhibit-agent --port /dev/ttyUSB0 --addr 127.0.0.1:22372\n"
          << "  inhibit-agent COM7 192.168.1.40:22372\n"
          << "  inhibit-agent --list-ports\n"
          << "GUI (no args): inhibit-agent-gui\n";
      return 2;
    }

  QString host;
  quint16 dest_port = 0;
  QString perr;
  if (!InhibitAgent::parse_dest_addr (addr, &host, &dest_port, &perr))
    {
      err << "inhibit-agent: " << perr << '\n';
      return 2;
    }

  int ttl = parser.value (ttlOpt).toInt ();
  if (ttl < 100 || ttl > 30000)
    {
      err << "ttl-ms must be 100..30000\n";
      return 2;
    }

  InhibitAgent::Config cfg;
  cfg.serial_port = port;
  cfg.dest_host = host;
  cfg.dest_port = dest_port;
  cfg.invert = parser.isSet (invertOpt);
  cfg.hold_timeout_ms = ttl;

  InhibitAgent agent (cfg);
  QObject::connect (&agent, &InhibitAgent::stateChanged, &app,
                    [&] (AgentState s, QString detail) {
                      out << QDateTime::currentDateTime ().toString (
                          QStringLiteral ("hh:mm:ss.zzz"))
                          << "  STATE " << InhibitAgent::state_name (s);
                      if (!detail.isEmpty ())
                        {
                          out << "  " << detail;
                        }
                      out << '\n';
                      out.flush ();
                    });
  QObject::connect (&agent, &InhibitAgent::logLine, &app,
                    [&] (QString line) {
                      out << QDateTime::currentDateTime ().toString (
                          QStringLiteral ("hh:mm:ss.zzz"))
                          << "  " << line << '\n';
                      out.flush ();
                    });

  if (!agent.start ())
    {
      err << "inhibit-agent: " << agent.fault_reason () << '\n';
      return 1;
    }

  std::signal (SIGINT, request_quit);
  std::signal (SIGTERM, request_quit);
#if defined (Q_OS_WIN)
  SetConsoleCtrlHandler (console_ctrl, TRUE);
#endif

  QObject::connect (&app, &QCoreApplication::aboutToQuit, &agent,
                    [&] () { agent.stop (); });

  out << "inhibit-agent  port=" << cfg.serial_port
      << "  addr=" << agent.dest_text ()
      << (cfg.invert ? "  invert" : "")
      << "\n  CTS asserted = KEY.  Ctrl-C releases and quits.\n";
  out.flush ();
  return app.exec ();
}
