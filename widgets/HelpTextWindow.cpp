#include "HelpTextWindow.hpp"

#include <QApplication>
#include <QString>
#include <QPalette>

#include "qt_helpers.hpp"

#include "moc_HelpTextWindow.cpp"

HelpTextWindow::HelpTextWindow (QString const& title, QString const& text, QFont const& font, QWidget * parent)
  : QLabel {parent, Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint}
{
  setWindowTitle(QApplication::applicationName () + " - " + title);
  setMargin (10);
  setBackgroundRole (QPalette::Base);
  setAutoFillBackground (true);
  setStyleSheet (font_as_stylesheet (font));
  setText (text);
  setMinimumSize (sizeHint ());
}
