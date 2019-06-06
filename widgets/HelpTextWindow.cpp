#include "HelpTextWindow.hpp"

#include <QApplication>
#include <QString>
#include <QPalette>
#include <QFile>
#include <QTextStream>

#include "qt_helpers.hpp"
#include "widgets/MessageBox.hpp"

#include "moc_HelpTextWindow.cpp"

HelpTextWindow::HelpTextWindow (QString const& title, QString const& file_name, QFont const& font, QWidget * parent)
  : QLabel {parent, Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint}
{
  QFile source {file_name};
  if (!source.open (QIODevice::ReadOnly | QIODevice::Text))
    {
      MessageBox::warning_message (this, tr ("Help file error")
                                   , tr ("Cannot open \"%1\" for reading").arg (source.fileName ())
                                   , tr ("Error: %1").arg (source.errorString ()));
      return;
    }
  setText (QTextStream {&source}.readAll ());
  setWindowTitle(QApplication::applicationName () + " - " + title);
  setMargin (10);
  setBackgroundRole (QPalette::Base);
  setAutoFillBackground (true);
  setStyleSheet (font_as_stylesheet (font));
}
