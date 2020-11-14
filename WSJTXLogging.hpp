#ifndef WSJTX_LOGGING_HPP__
#define WSJTX_LOGGING_HPP__

#include <QtGlobal>

class QString;

//
// Class WSJTXLogging - wraps application specific logging
//
class WSJTXLogging final
{
public:
  explicit WSJTXLogging ();
  ~WSJTXLogging ();

  //
  // Install this as the Qt message handler (qInstallMessageHandler)
  // to integrate Qt messages. This handler can be installed at any
  // time, it does not rely on an instance of WSJTXLogging existing,
  // so logging occurring before the logging sinks, filters, and
  // formatters, etc, are established can take place.
  static void qt_log_handler (QtMsgType type, QMessageLogContext const& context, QString const&);
};

#endif
