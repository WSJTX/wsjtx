#ifndef WAV_LOAD_COORDINATOR_HPP
#define WAV_LOAD_COORDINATOR_HPP

#include <functional>
#include <memory>

#include <QFutureWatcher>
#include <QObject>

#include "Audio/WavInputLoader.hpp"

class WavLoadCoordinator : public QObject
{
  Q_OBJECT

public:
  using Result = std::shared_ptr<Radio::WavInputResult>;
  using Work = std::function<Result ()>;

  explicit WavLoadCoordinator (QObject * parent = nullptr);
  ~WavLoadCoordinator () override;

  bool start (Work work);
  bool isLoading () const;
  Result result () const;
  void waitForFinished ();

Q_SIGNALS:
  void loadingChanged (bool loading);
  void resultReady ();

private:
  void handleFinished ();

  bool loading_ {false};
  Result result_;
  QFutureWatcher<Result> watcher_;
};

#endif // WAV_LOAD_COORDINATOR_HPP
