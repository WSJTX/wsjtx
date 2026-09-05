#ifndef RECEIVE_HANDOFF_TEST_CONTROLLER_HPP
#define RECEIVE_HANDOFF_TEST_CONTROLLER_HPP

#include <condition_variable>
#include <memory>
#include <mutex>

#include <QObject>
#include <QTimer>

class FixtureAudioInput;
class MainWindow;

// Drives capture checkpoints while controlling delivery to the real GUI consumer.
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
  bool advance (qint64 frames, bool fresh = false, bool flush = false);
  bool drain ();
  bool require (bool condition, QString const& message);
  void resetScenario (QString const& name);
  void observe (quint64 epoch, int start, int end, bool accepted);
  QString diagnostics () const;
  void finish (bool success, QString const& message);

  MainWindow * window_;
  FixtureAudioInput * fixture_;
  QTimer retry_;
  QTimer timeout_;
  struct ProducerAcknowledgement
  {
    std::mutex mutex;
    std::condition_variable changed;
    QString error;
    bool finished {false};
    qint64 frames {-1};
  };
  std::shared_ptr<ProducerAcknowledgement> producer_ {
    std::make_shared<ProducerAcknowledgement> ()};
  qint64 capture_ {0};
  QString scenario_;
  QString scenario_error_;
  quint64 epoch_ {0};
  quint64 obsolete_epoch_ {0};
  qint64 sample_period_ {0};
  int accepted_ = 0;
  int rejected_ = 0;
  int committed_ = 0;
  int rejected_end_ = 0;
  int final_submissions_ = 0;
  quint64 decoder_generation_ = 0;
  bool finished_ {false};
  bool succeeded_ {false};
};

#endif
