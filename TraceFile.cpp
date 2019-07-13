#include "TraceFile.hpp"

#include <stdexcept>

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QFile>
#include <QTextStream>

#include "pimpl_impl.hpp"

class TraceFile::impl
{
public:
  impl (QString const& trace_file_path);
  ~impl ();

  // no copying
  impl (impl const&) = delete;
  impl& operator = (impl const&) = delete;

private:
  // write Qt messages to the diagnostic log file
  static void message_handler (QtMsgType type, QMessageLogContext const& context, QString const& msg);

  QFile file_;
  QTextStream stream_;
  QTextStream * original_stream_;
  QtMessageHandler original_handler_;
  static QTextStream * current_stream_;
  static QMutex mutex_;
};

QTextStream * TraceFile::impl::current_stream_;
QMutex TraceFile::impl::mutex_;

// delegate to implementation class
TraceFile::TraceFile (QString const& trace_file_path)
  : m_ {trace_file_path}
{
}

TraceFile::~TraceFile ()
{
}


TraceFile::impl::impl (QString const& trace_file_path)
  : file_ {trace_file_path}
  , original_stream_ {current_stream_}
  , original_handler_ {nullptr}
{
  // if the log file is writeable; initialise diagnostic logging to it
  // for append and hook up the Qt global message handler
  if (file_.open (QFile::WriteOnly | QFile::Append | QFile::Text))
    {
      stream_.setDevice (&file_);
      current_stream_ = &stream_;
      original_handler_ = qInstallMessageHandler (message_handler);
    }
}

TraceFile::impl::~impl ()
{
  // unhook our message handler before the stream and file are destroyed
  if (original_handler_)
    {
      qInstallMessageHandler (original_handler_);
    }
  current_stream_ = original_stream_; // revert to prior stream
}

// write Qt messages to the diagnostic log file
void TraceFile::impl::message_handler (QtMsgType type, QMessageLogContext const& context, QString const& msg)
{
  Q_ASSERT_X (current_stream_, "TraceFile:message_handler", "no stream to write to");
  {
    QMutexLocker lock {&mutex_}; // thread safety - serialize writes to the trace file
    *current_stream_ << qFormatLogMessage (type, context, msg) << endl;
  }

  if (QtFatalMsg == type)
    {
      throw std::runtime_error {"Fatal Qt Error"};
    }
}
