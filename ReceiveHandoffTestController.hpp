#ifndef RECEIVE_HANDOFF_TEST_CONTROLLER_HPP
#define RECEIVE_HANDOFF_TEST_CONTROLLER_HPP

#include <condition_variable>
#include <mutex>

#include <QObject>
#include <QTimer>

class FixtureAudioInput;
class MainWindow;

// Test-only controller for the full MainWindow receive boundary.  It withholds
// GUI event processing until the audio-thread fixture has produced and reused
// two complete periods, then validates the first queued notification.
class ReceiveHandoffTestController final : public QObject
{
  Q_OBJECT

public:
  ReceiveHandoffTestController (MainWindow * window, FixtureAudioInput * fixture,
                                QObject * parent = nullptr);

  void begin ();
  bool succeeded () const {return succeeded_;}

private:
  void prepare ();
  void finish (bool success, QString const& message);

  MainWindow * window_;
  FixtureAudioInput * fixture_;
  QTimer retry_;
  QTimer timeout_;
  std::mutex producer_mutex_;
  std::condition_variable producer_changed_;
  QString producer_error_;
  bool producer_finished_ {false};
  bool callback_observed_ {false};
  int rejected_ = 0;
  bool finished_ {false};
  bool succeeded_ {false};
};

#endif
