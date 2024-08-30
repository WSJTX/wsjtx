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
  ui->header_label2->setText("  N   Call    Grid   Az  S/N  Freq Tx Age Pts");
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
  this->displayRecentStations(m_mode, m_textbuffer);
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

void ActiveStations::setupUi(QString mode) {
  if(mode!=m_mode) {
    m_mode=mode;
    ui->cbReadyOnly->setText(" Ready only");
    if(m_mode=="Q65") {
      ui->header_label2->setText("  N    Frx   Fsked  S/N  Q65  Call     Grid  Tx  Age");
      ui->label->setText("QSOs:");
      ui->cbReadyOnly->setText("* CQ only");
    } else if(m_mode=="Q65-pileup") {
      ui->header_label2->setText("  N   Freq  Call    Grid   El   Age(h)");
      ui->cbWantedOnly->setText(QCoreApplication::translate("ActiveStations", "Wanted only", nullptr));
    } else if(m_mode=="Fox Mode" || m_mode=="SuperFox Mode" ) {
      ui->header_label2->setText("  UTC   dB   DT Freq    " + tr("Message"));
      ui->cbWantedOnly->setText(QCoreApplication::translate("ActiveStations", "My call only", nullptr));
      this->setClickOK(true);
    } else {
      ui->header_label2->setText("  N   Call    Grid   Az  S/N  Freq Tx Age Pts");
      ui->label->setText("Rate:");
      ui->cbWantedOnly->setText(QCoreApplication::translate("ActiveStations", "Wanted only", nullptr));
    }
    bool b=(m_mode.left(3)=="Q65");
    bool is_fox_mode =(m_mode=="Fox Mode");
    ui->bandChanges->setVisible(!b && !is_fox_mode);
    ui->cbReadyOnly->setVisible(m_mode != "Q65-pileup" && !is_fox_mode);
    ui->cbWantedOnly->setVisible(m_mode != "Q65-pileup"); // this is used for "My call only" in Fox mode
    ui->label_2->setVisible(!b && !is_fox_mode);
    ui->label_3->setVisible(!b && !is_fox_mode);
    ui->score->setVisible(!b && !is_fox_mode);
    ui->sbMaxRecent->setVisible(!b && !is_fox_mode);

    b=(m_mode!="Q65-pileup" && !is_fox_mode);
    ui->sbMaxAge->setVisible(b);
    ui->label->setVisible(b);
    ui->rate->setVisible(b);
  }
}

void ActiveStations::displayRecentStations(QString mode, QString const& t)
{
  setupUi(mode);

  bool bClickOK=m_clickOK;
  m_clickOK=false;
  ui->RecentStationsPlainTextEdit->setPlainText(t);

//White background for Q65-60x decodes, yellow for Q65-30x:
  int i0=0;
  int i1=0;
  int npos=0;
  int nlines=t.count("\n");
  QTextCursor cursor=ui->RecentStationsPlainTextEdit->textCursor();
  QTextCharFormat fmt;
  for(int i=0; i<nlines; i++) {
    i1=t.indexOf("\n",i0);
    npos=t.indexOf(QRegularExpression(" 30[ABCD] "), i0);
    if(npos>0) {
      cursor.setPosition(npos);
      cursor.select(QTextCursor::LineUnderCursor);
      fmt.setBackground(QBrush(Qt::yellow));
      fmt.setForeground(QBrush(Qt::black));
    } else {
      cursor.setPosition(i0+10);
      cursor.select(QTextCursor::LineUnderCursor);
      fmt.clearForeground();
      fmt.clearBackground();
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
      if ("Fox Mode" != m_mode && "SuperFox Mode" != m_mode)
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
