#ifndef AUTORESPONDSELECTIONLATCH_HPP
#define AUTORESPONDSELECTIONLATCH_HPP

#include <QObject>
#include <QTimer>

class AutoRespondSelectionLatch : public QObject
{
public:
  enum { DefaultResetDelayMs = 6000 };

  explicit AutoRespondSelectionLatch(QObject * parent = nullptr);

  bool isSelected() const;
  void selectFor(int delayMs = DefaultResetDelayMs);
  void clear();

private:
  QTimer m_resetTimer;
  bool m_selected {false};
};

#endif
