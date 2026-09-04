#include "TciSimServer.hpp"

#include <utility>

#include <QEventLoop>
#include <QHostAddress>
#include <QTimer>
#include <QUrl>
#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketServer>

#include "moc_TciSimServer.cpp"

TciSimServer::TciSimServer (QObject * parent)
  : QObject {parent}
  , server_ {new QWebSocketServer {QStringLiteral ("TCI simulator"),
                                  QWebSocketServer::NonSecureMode, this}}
  , client_ {nullptr}
{
  connect (server_, &QWebSocketServer::newConnection,
           this, &TciSimServer::on_new_connection);
}

TciSimServer::~TciSimServer ()
{
  close ();
}

bool TciSimServer::listen ()
{
  if (server_->isListening ()) return true;
  if (!server_->listen (QHostAddress::LocalHost, 0)) return false;
  port_ = server_->serverPort ();
  return true;
}

void TciSimServer::close ()
{
  if (client_)
    {
      client_->close (QWebSocketProtocol::CloseCodeNormal,
                      QStringLiteral ("simulator done"));
      client_ = nullptr;
    }
  server_->close ();
  port_ = 0;
}

quint16 TciSimServer::port () const
{
  return port_;
}

QString TciSimServer::url () const
{
  return QStringLiteral ("ws://127.0.0.1:%1").arg (port_);
}

bool TciSimServer::client_connected () const
{
  return client_ && client_->state () == QAbstractSocket::ConnectedState;
}

bool TciSimServer::client_disconnected () const
{
  return !client_;
}

int TciSimServer::connection_generation () const
{
  return connection_generation_;
}

int TciSimServer::connection_count () const
{
  return connection_count_;
}

void TciSimServer::send_text (QString text)
{
  if (!client_connected ()) return;
  if (!text.endsWith (';')) text += ';';
  record_text (Direction::ServerToClient, text, connection_generation_);
  client_->sendTextMessage (text);
}

bool TciSimServer::send_binary (QByteArray const& data)
{
  return client_connected () && client_->sendBinaryMessage (data) == data.size ();
}

bool TciSimServer::flush ()
{
  return client_connected () && client_->flush ();
}

void TciSimServer::send_text_later (QString text, int delay_ms)
{
  auto const generation = connection_generation_;
  QTimer::singleShot (qMax (0, delay_ms), this,
                      [this, generation, text = std::move (text)] () mutable
                      {
                        if (client_connected ()
                            && connection_generation_ == generation)
                          {
                            send_text (std::move (text));
                          }
                      });
}

void TciSimServer::close_client (QString const& reason)
{
  if (client_)
    {
      client_->close (QWebSocketProtocol::CloseCodeNormal, reason);
    }
}

void TciSimServer::abort_client ()
{
  if (client_) client_->abort ();
}

QVector<TciSimServer::Message> const& TciSimServer::transcript () const
{
  return transcript_;
}

QVector<TciSimServer::Frame> const& TciSimServer::frames () const
{
  return frames_;
}

TciSimServer::Cursor TciSimServer::cursor (int start_index) const
{
  return {qBound (0, start_index, transcript_.size ())};
}

bool TciSimServer::consume (
  Cursor * cursor_value, Direction direction, QString const& verb,
  Message * message,
  std::function<bool (QStringList const&)> const& predicate,
  int connection_generation) const
{
  if (!cursor_value) return false;

  for (int i = qMax (0, cursor_value->next_index); i < transcript_.size (); ++i)
    {
      auto const& candidate = transcript_.at (i);
      cursor_value->next_index = i + 1;
      if (candidate.direction != direction || candidate.verb != verb) continue;
      if (connection_generation > 0
          && candidate.connection_generation != connection_generation) continue;
      if (predicate && !predicate (candidate.args)) continue;
      if (message) *message = candidate;
      return true;
    }
  cursor_value->next_index = transcript_.size ();
  return false;
}

QVector<TciSimServer::Message> TciSimServer::messages (
  Direction direction, QString const& verb) const
{
  QVector<Message> result;
  for (auto const& message : transcript_)
    {
      if (message.direction == direction
          && (verb.isEmpty () || message.verb == verb))
        {
          result.append (message);
        }
    }
  return result;
}

int TciSimServer::count (Direction direction, QString const& verb) const
{
  int result {0};
  for (auto const& message : transcript_)
    {
      if (message.direction == direction
          && (verb.isEmpty () || message.verb == verb))
        {
          ++result;
        }
    }
  return result;
}

int TciSimServer::index_of (
  Direction direction, QString const& verb, int occurrence) const
{
  if (occurrence < 0) return -1;
  for (int i = 0; i < transcript_.size (); ++i)
    {
      auto const& message = transcript_.at (i);
      if (message.direction == direction && message.verb == verb
          && occurrence-- == 0)
        {
          return i;
        }
    }
  return -1;
}

bool TciSimServer::occurs_before (
  Direction first_direction, QString const& first_verb,
  Direction second_direction, QString const& second_verb) const
{
  auto const first = index_of (first_direction, first_verb);
  auto const second = index_of (second_direction, second_verb);
  return first >= 0 && second >= 0 && first < second;
}

QString TciSimServer::transcript_text () const
{
  QStringList lines;
  lines.reserve (frames_.size ());
  for (auto const& frame : frames_)
    {
      auto const arrow = frame.direction == Direction::ClientToServer
        ? QStringLiteral ("client -> server")
        : QStringLiteral ("server -> client");
      lines.append (QStringLiteral ("frame #%1 connection %2 %3: %4")
                    .arg (frame.ordinal)
                    .arg (frame.connection_generation)
                    .arg (arrow, frame.raw));
    }
  return lines.join ('\n');
}

bool TciSimServer::wait_for (
  std::function<bool ()> const& predicate, int timeout_ms)
{
  if (predicate ()) return true;

  QEventLoop loop;
  QTimer poller;
  QTimer timeout;
  bool matched {false};
  poller.setInterval (5);
  timeout.setSingleShot (true);
  connect (&poller, &QTimer::timeout, &loop,
           [&predicate, &matched, &loop] ()
           {
             if (!predicate ()) return;
             matched = true;
             loop.quit ();
           });
  connect (&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  poller.start ();
  timeout.start (qMax (0, timeout_ms));
  loop.exec ();
  return matched || predicate ();
}

void TciSimServer::on_new_connection ()
{
  while (auto * candidate = server_->nextPendingConnection ())
    {
      if (client_)
        {
          candidate->close (QWebSocketProtocol::CloseCodePolicyViolated,
                            QStringLiteral ("one client at a time"));
          candidate->deleteLater ();
          continue;
        }

      client_ = candidate;
      connection_generation_ = ++connection_count_;
      client_->setProperty ("tciConnectionGeneration", connection_generation_);
      connect (client_, &QWebSocket::textMessageReceived,
               this, &TciSimServer::on_text_message);
      connect (client_, &QWebSocket::disconnected,
               this, &TciSimServer::on_disconnected);
      emit client_arrived (connection_generation_);
    }
}

void TciSimServer::on_text_message (QString const& text)
{
  auto * socket = qobject_cast<QWebSocket *> (sender ());
  auto const generation = socket
    ? socket->property ("tciConnectionGeneration").toInt ()
    : connection_generation_;
  record_text (Direction::ClientToServer, text, generation);
  emit text_received (text, generation);
}

void TciSimServer::on_disconnected ()
{
  auto * socket = qobject_cast<QWebSocket *> (sender ());
  auto const generation = socket
    ? socket->property ("tciConnectionGeneration").toInt ()
    : connection_generation_;
  if (socket == client_) client_ = nullptr;
  if (socket) socket->deleteLater ();
  emit client_gone (generation);
}

void TciSimServer::record_text (
  Direction direction, QString const& text, int generation)
{
  frames_.append ({direction, generation, next_frame_ordinal_++, text});
  auto const commands = text.split (';', Qt::SkipEmptyParts);
  for (auto const& command : commands)
    {
      transcript_.append (parse_message (direction, generation,
                                         next_ordinal_++, command));
    }
}

TciSimServer::Message TciSimServer::parse_message (
  Direction direction, int generation, quint64 ordinal, QString const& raw)
{
  Message result {direction, generation, ordinal, raw, {}, {}};
  auto const separator = raw.indexOf (':');
  if (separator < 0)
    {
      result.verb = raw.trimmed ();
      return result;
    }

  result.verb = raw.left (separator).trimmed ();
  result.args = raw.mid (separator + 1).split (',', Qt::KeepEmptyParts);
  return result;
}
