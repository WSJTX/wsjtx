#ifndef TCI_SIM_SCRIPT_HPP__
#define TCI_SIM_SCRIPT_HPP__

#include <functional>
#include <vector>

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "TciSimServer.hpp"

enum class TciSimGreeting
{
  Thetis,
  Minimal,
};

class TciSimScript final
  : public QObject
{
  Q_OBJECT

public:
  using ArgsPredicate = std::function<bool (QStringList const&)>;

  explicit TciSimScript (TciSimServer * server, QObject * parent = nullptr);

  TciSimScript& greet (TciSimGreeting greeting = TciSimGreeting::Thetis,
                       int timeout_ms = 2000);
  TciSimScript& expect (QString verb, ArgsPredicate predicate = {},
                        int timeout_ms = 2000);
  TciSimScript& reply (QString text, int delay_ms = 0);
  TciSimScript& push (QString text, int delay_ms = 0);
  TciSimScript& withhold (QString verb, ArgsPredicate predicate = {},
                          int timeout_ms = 2000);
  TciSimScript& delay (int delay_ms);
  TciSimScript& close_normally (
    QString reason = QStringLiteral ("scripted disconnect"));
  TciSimScript& reconnect (int timeout_ms = 2000);

  void start (int overall_timeout_ms = 10000);
  bool run (int overall_timeout_ms = 10000);
  void stop ();

  bool running () const;
  bool succeeded () const;
  int failed_step () const;
  QString last_error () const;

  static QStringList greeting_messages (TciSimGreeting greeting);

signals:
  void finished (bool succeeded);

private slots:
  void execute_step ();
  void poll_step ();
  void delayed_step_ready ();
  void overall_timeout ();

private:
  enum class StepKind
  {
    Greet,
    Expect,
    Reply,
    Push,
    Withhold,
    Delay,
    CloseNormally,
    Reconnect,
  };

  struct Step
  {
    StepKind kind;
    QString text;
    ArgsPredicate predicate;
    int timeout_ms {0};
    TciSimGreeting greeting {TciSimGreeting::Minimal};
  };

  void append_expectation (StepKind kind, QString verb,
                           ArgsPredicate predicate, int timeout_ms);
  void start_polling (int timeout_ms);
  void complete_step ();
  void fail (QString reason);
  void finish (bool succeeded);
  bool current_expectation_satisfied ();
  QString step_description (Step const& step) const;

  TciSimServer * server_;
  std::vector<Step> steps_;
  TciSimServer::Cursor cursor_;
  QTimer poll_timer_;
  QTimer delay_timer_;
  QTimer overall_timer_;
  QElapsedTimer step_elapsed_;
  int current_step_ {-1};
  int reconnect_after_generation_ {0};
  int active_connection_generation_ {0};
  int last_consumed_generation_ {0};
  int delayed_step_generation_ {0};
  int close_generation_ {0};
  bool close_observed_ {false};
  bool has_consumed_expectation_ {false};
  bool close_requested_ {false};
  bool running_ {false};
  bool succeeded_ {false};
  QString last_error_;
};

#endif // TCI_SIM_SCRIPT_HPP__
