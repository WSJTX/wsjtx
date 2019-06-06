#include "messageaveraging.h"

#include <QSettings>
#include <QApplication>
#include <QTextCharFormat>

#include "SettingsGroup.hpp"
#include "qt_helpers.hpp"
#include "ui_messageaveraging.h"

#include "moc_messageaveraging.cpp"

MessageAveraging::MessageAveraging(QSettings * settings, QFont const& font, QWidget *parent) :
  QWidget(parent),
  settings_ {settings},
  ui(new Ui::MessageAveraging)
{
  ui->setupUi(this);
  setWindowTitle (QApplication::applicationName () + " - " + tr ("Message Averaging"));
  ui->msgAvgPlainTextEdit->setReadOnly (true);
  changeFont (font);
  read_settings ();
  ui->header_label->setText("   UTC  Sync    DT  Freq   ");
}

MessageAveraging::~MessageAveraging()
{
  write_settings ();
}

void MessageAveraging::changeFont (QFont const& font)
{
  ui->header_label->setStyleSheet (font_as_stylesheet (font));
  ui->msgAvgPlainTextEdit->setStyleSheet (font_as_stylesheet (font));
  setContentFont (font);
  updateGeometry ();
}

void MessageAveraging::setContentFont(QFont const& font)
{
  ui->msgAvgPlainTextEdit->setFont (font);
  QTextCharFormat charFormat;
  charFormat.setFont (font);
  ui->msgAvgPlainTextEdit->selectAll ();
  auto cursor = ui->msgAvgPlainTextEdit->textCursor ();
  cursor.mergeCharFormat (charFormat);
  cursor.clearSelection ();
  cursor.movePosition (QTextCursor::End);

  // position so viewport scrolled to left
  cursor.movePosition (QTextCursor::Up);
  cursor.movePosition (QTextCursor::StartOfLine);

  ui->msgAvgPlainTextEdit->setTextCursor (cursor);
  ui->msgAvgPlainTextEdit->ensureCursorVisible ();
}

void MessageAveraging::read_settings ()
{
  SettingsGroup group {settings_, "MessageAveraging"};
  restoreGeometry (settings_->value ("window/geometry").toByteArray ());
}

void MessageAveraging::write_settings ()
{
  SettingsGroup group {settings_, "MessageAveraging"};
  settings_->setValue ("window/geometry", saveGeometry ());
}

void MessageAveraging::displayAvg(QString const& t)
{
  ui->msgAvgPlainTextEdit->setPlainText(t);
}
