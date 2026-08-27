#ifndef TCI_SIM_SERVER_HPP__
#define TCI_SIM_SERVER_HPP__

#include <functional>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QWebSocket;
class QWebSocketServer;

class TciSimServer final
  : public QObject
{
  Q_OBJECT

public:
  enum class Direction
  {
    ClientToServer,
    ServerToClient,
  };

  struct Message
  {
    Direction direction;
    int connection_generation;
    quint64 ordinal;
    QString raw;
    QString verb;
    QStringList args;
  };

  struct Frame
  {
    Direction direction;
    int connection_generation;
    quint64 ordinal;
    QString raw;
  };

  struct Cursor
  {
    int next_index {0};
  };

  explicit TciSimServer (QObject * parent = nullptr);
  ~TciSimServer () override;

  bool listen ();
  void close ();
  quint16 port () const;
  QString url () const;

  bool client_connected () const;
  bool client_disconnected () const;
  int connection_generation () const;
  int connection_count () const;

  void send_text (QString text);
  void send_text_later (QString text, int delay_ms);
  void close_client (QString const& reason = QStringLiteral ("simulator close"));
  void abort_client ();

  QVector<Message> const& transcript () const;
  QVector<Frame> const& frames () const;
  Cursor cursor (int start_index = 0) const;
  bool consume (Cursor * cursor, Direction direction, QString const& verb,
                Message * message = nullptr,
                std::function<bool (QStringList const&)> const& predicate = {},
                int connection_generation = 0) const;
  QVector<Message> messages (Direction direction, QString const& verb = {}) const;
  int count (Direction direction, QString const& verb = {}) const;
  int index_of (Direction direction, QString const& verb, int occurrence = 0) const;
  bool occurs_before (Direction first_direction, QString const& first_verb,
                      Direction second_direction, QString const& second_verb) const;
  QString transcript_text () const;

  bool wait_for (std::function<bool ()> const& predicate, int timeout_ms);

signals:
  void client_arrived (int connection_generation);
  void client_gone (int connection_generation);
  void text_received (QString const& raw, int connection_generation);

private slots:
  void on_new_connection ();
  void on_text_message (QString const& text);
  void on_disconnected ();

private:
  void record_text (Direction direction, QString const& text, int generation);
  static Message parse_message (Direction direction, int generation,
                                quint64 ordinal, QString const& raw);

  QWebSocketServer * server_;
  QWebSocket * client_;
  quint16 port_ {0};
  int connection_generation_ {0};
  int connection_count_ {0};
  quint64 next_ordinal_ {0};
  quint64 next_frame_ordinal_ {0};
  QVector<Message> transcript_;
  QVector<Frame> frames_;
};

#endif // TCI_SIM_SERVER_HPP__
