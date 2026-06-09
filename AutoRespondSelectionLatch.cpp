#include "AutoRespondSelectionLatch.hpp"

AutoRespondSelectionLatch::AutoRespondSelectionLatch(QObject * parent)
  : QObject {parent}
{
  m_resetTimer.setSingleShot(true);
  connect(&m_resetTimer, &QTimer::timeout, this, [this] { clear(); });
}

bool AutoRespondSelectionLatch::isSelected() const
{
  return m_selected;
}

void AutoRespondSelectionLatch::selectFor(int delayMs)
{
  m_selected = true;
  m_resetTimer.start(delayMs);
}

void AutoRespondSelectionLatch::clear()
{
  m_resetTimer.stop();
  m_selected = false;
}
