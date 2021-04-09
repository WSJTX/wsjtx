#include "displaytext.h"
#include <QDebug>
#include <QMouseEvent>

DisplayText::DisplayText(QWidget *parent) :
    QTextBrowser(parent)
{
}

void DisplayText::mouseDoubleClickEvent(QMouseEvent *e)
{
  bool ctrl = (e->modifiers() & 0x4000000);
  emit(selectCallsign(ctrl));
  QTextBrowser::mouseDoubleClickEvent(e);
}
