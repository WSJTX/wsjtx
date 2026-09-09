#include "activeStations.h"

#include <QSettings>
#include <QApplication>
#include <QTextCharFormat>
#include <QDateTime>
#include <QDebug>

#include "SettingsGroup.hpp"
#include "qt_helpers.hpp"
#include "ui_activeStations.h"

#include "moc_activeStations.cpp"

ActiveStations::ActiveStations(QSettings * settings, QFont const& font, QWidget *parent) :
  QWidget(parent),
  settings_ {settings},
  ui(new Ui::ActiveStations)
{
  ui->setupUi(this);
  setWindowTitle (QApplication::applicationName () + " - " + tr ("Active Stations"));
  ui->RecentStationsPlainTextEdit->setReadOnly (true);
  changeFont (font);
  read_settings ();
  setupUi(DisplayMode::Standard);
  connect(ui->cbReadyOnly, SIGNAL(toggled(bool)), this, SLOT(on_cbReadyOnly_toggled(bool)));
  connect(ui->cbWantedOnly, SIGNAL(toggled(bool)), this, SLOT(on_cbWantedOnly_toggled(bool)));
  connect(ui->RecentStationsPlainTextEdit, SIGNAL(cursorPositionChanged()), this, SLOT(on_textEdit_clicked()));
}

ActiveStations::~ActiveStations()
{
  write_settings ();
}

void ActiveStations::changeFont (QFont const& font)
{
  ui->header_label2->setStyleSheet (font_as_stylesheet (font));
  ui->RecentStationsPlainTextEdit->setStyleSheet (font_as_stylesheet (font));
  updateGeometry ();
}

void ActiveStations::clearStations() {
  m_textbuffer.clear();
  m_decodes_by_frequency.clear();
}

void ActiveStations::addLine(QString line) {
  QString m_textbuffer = "";
  // "012700  -1  0.2  210 ~  KJ7COA JA2HGF -14"
  unsigned freq = line.mid(16, 4).toUInt();
  m_decodes_by_frequency[freq] = line;
  // show them in frequency order
  QMap<int, QString>::const_iterator i = m_decodes_by_frequency.constBegin();
  m_textbuffer.clear();
  while (i != m_decodes_by_frequency.constEnd()) {
    m_textbuffer.append(i.value());
    ++i;
  }
  this->displayRecentStations(m_displayMode, m_textbuffer);
}

void ActiveStations::read_settings ()
{
  SettingsGroup group {settings_, "ActiveStations"};
  restoreGeometry (settings_->value ("window/geometry").toByteArray ());
  ui->sbMaxRecent->setValue(settings_->value("MaxRecent",10).toInt());
  ui->sbMaxAge->setValue(settings_->value("MaxAge",10).toInt());
  ui->cbReadyOnly->setChecked(settings_->value("ReadyOnly",false).toBool());
  ui->cbWantedOnly->setChecked(settings_->value("# WantedOnly",false).toBool());
}

void ActiveStations::write_settings ()
{
  SettingsGroup group {settings_, "ActiveStations"};
  settings_->setValue ("window/geometry", saveGeometry ());
  settings_->setValue("MaxRecent",ui->sbMaxRecent->value());
  settings_->setValue("MaxAge",ui->sbMaxAge->value());
  settings_->setValue("ReadyOnly",ui->cbReadyOnly->isChecked());
  settings_->setValue("WantedOnly",ui->cbWantedOnly->isChecked());
}

void ActiveStations::setupUi(DisplayMode mode) {
  if (mode != m_displayMode && mode == DisplayMode::Fox) setClickOK(true);
  m_displayMode=mode;
  ui->cbReadyOnly->setText(" Ready only");
  ui->cbWantedOnly->setText(tr("Wanted only"));
  ui->label->setText("Rate:");
  if(mode==DisplayMode::Q65) {
    ui->header_label2->setText("  N    Frx   Fsked  S/N  Q65  Call     Grid  Tx  Age");
    ui->label->setText("QSOs:");
    ui->cbReadyOnly->setText("* CQ only");
  } else if(mode==DisplayMode::Q65Pileup) {
    ui->header_label2->setText("  N   Freq  Call    Grid   El   Age(h)");
  } else if(mode==DisplayMode::Fox) {
    ui->header_label2->setText("  UTC   dB   DT Freq    " + tr("Message"));
    ui->cbWantedOnly->setText(tr("My call only"));
  } else {
    ui->header_label2->setText("  N   Call    Grid   Az  S/N  Freq Tx Age Pts");
  }
  bool const standard = mode == DisplayMode::Standard;
  bool const numbered = standard || mode == DisplayMode::Q65;
  ui->bandChanges->setVisible(standard);
  ui->cbReadyOnly->setVisible(numbered);
  ui->cbWantedOnly->setVisible(mode != DisplayMode::Q65Pileup);
  ui->label_2->setVisible(standard);
  ui->label_3->setVisible(standard);
  ui->score->setVisible(standard);
  ui->sbMaxRecent->setVisible(standard);
  ui->sbMaxAge->setVisible(numbered);
  ui->label->setVisible(numbered);
  ui->rate->setVisible(numbered);
}

void ActiveStations::displayRecentStations(DisplayMode mode, QString const& t)
{
  setupUi(mode);

  bool bClickOK=m_clickOK;
  m_clickOK=false;
  ui->RecentStationsPlainTextEdit->setPlainText(t);

  int i0=0;
  int i1=0;
  int nlines=t.count("\n");
  QTextCursor cursor=ui->RecentStationsPlainTextEdit->textCursor();
  QTextCharFormat fmt;

  // Use a regular expression matcher to find the text pattern within each line bounds
  QRegularExpression regex(" 30[ABCD] ");

  for(int i=0; i<nlines; i++) {
    i1=t.indexOf("\n",i0);
    // Isolate the current line to check for the match
    QString currentLine = t.mid(i0, i1 - i0);
    bool hasMatch = currentLine.contains(regex);

    // Move the cursor to the current line we are processing
    cursor.setPosition(i0);
    cursor.select(QTextCursor::LineUnderCursor);

    if(hasMatch) {
      fmt.setBackground(QBrush(Qt::yellow));
      fmt.setForeground(QBrush(Qt::black));
    } else {
      // Explicitly force the background back to white
      fmt.setBackground(QBrush(Qt::white));
      fmt.setForeground(QBrush(Qt::black));
    }

    cursor.setCharFormat(fmt);
    i0=i1+1;
  }
  m_clickOK=bClickOK;
}

int ActiveStations::maxRecent()
{
  return ui->sbMaxRecent->value();
}

int ActiveStations::maxAge()
{
  return ui->sbMaxAge->value();
}

void ActiveStations::on_textEdit_clicked()
{
  if(m_clickOK) {
    QTextCursor cursor;
    QString text;
    cursor = ui->RecentStationsPlainTextEdit->textCursor();
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    text = cursor.selectedText();
    if(text!="") {
      int nline=text.left(2).toInt();
      if(QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) nline=-nline;
      if (DisplayMode::Fox != m_displayMode)
        emit callSandP(nline);
      else
        emit queueActiveWindowHound(text);
    }
  }
}

void ActiveStations::setClickOK(bool b)
{
  m_clickOK=b;
}

void ActiveStations::erase()
{
  ui->RecentStationsPlainTextEdit->clear();
}

bool ActiveStations::readyOnly()
{
  return ui->cbReadyOnly->isChecked();
}

void ActiveStations::on_cbReadyOnly_toggled(bool b)
{
  m_bReadyOnly=b;
  emit activeStationsDisplay();
}

bool ActiveStations::wantedOnly()
{
  return ui->cbWantedOnly->isChecked();
}

void ActiveStations::on_cbWantedOnly_toggled(bool b)
{
  m_bWantedOnly=b;
  emit activeStationsDisplay();
}

void ActiveStations::setRate(int n)
{
  ui->rate->setText(QString::number(n));
}

void ActiveStations::setScore(int n)
{
  ui->score->setText(QLocale(QLocale::English).toString(n));
}

void ActiveStations::setBandChanges(int n)
{
  if(n >= 8) {
    ui->bandChanges->setStyleSheet("QLineEdit{background: rgb(255, 64, 64)}");
  } else {
    ui->bandChanges->setStyleSheet ("");
  }
  ui->bandChanges->setText(QString::number(n));
}
