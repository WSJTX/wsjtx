#include "messages.h"
#include <QSettings>
#include "SettingsGroup.hpp"
#include "ui_messages.h"
#include "mainwindow.h"
#include "qt_helpers.hpp"

#include <QCoreApplication> //liveCQ
#include <QNetworkAccessManager> //liveCQ
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QEventLoop>

#include <iostream>
#include <string>

Messages::Messages (QString const& settings_filename, QWidget * parent) :
  QDialog {parent},
  ui {new Ui::Messages},
  m_settings_filename {settings_filename}
{
  ui->setupUi(this);
  setWindowTitle("Messages");
  setWindowFlags (Qt::Dialog | Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint);
  QSettings settings {m_settings_filename, QSettings::IniFormat};
  SettingsGroup g {&settings, "MainWindow"}; // MainWindow group for
                                             // historical reasons
  setGeometry (settings.value ("MessagesGeom", QRect {800, 400, 381, 400}).toRect ());
  ui->messagesTextBrowser->setStyleSheet( \
          "QTextBrowser { background-color : #000066; color : red; }");
  ui->messagesTextBrowser->clear();
  m_cqOnly=false;
  m_cqStarOnly=false;
  QString guiDate;
  QStringList allDecodes =  { "" };
  QString guiFreq;
  connect (ui->messagesTextBrowser, &DisplayText::selectCallsign, this, &Messages::selectCallsign2);
}

Messages::~Messages()
{
  QSettings settings {m_settings_filename, QSettings::IniFormat};
  SettingsGroup g {&settings, "MainWindow"};
  settings.setValue ("MessagesGeom", geometry ());
  delete ui;
}

void Messages::sendLiveCQData(QStringList decodeList)
{
  if(allDecodes.contains(decodeList.at(0))) return;
  allDecodes += decodeList;
  try {
    //  QMessageBox msgbox;
    //  msgbox.setText(QString::number(allDecodes.size()));
    //  msgbox.exec();
  }
  catch(...){}

  QSettings settings(m_settings_filename, QSettings::IniFormat);
  {
    SettingsGroup g {&settings, "MainWindow"};
  }

  SettingsGroup g {&settings, "Common"};
  bool m_w3szUrl = settings.value("w3szUrl",true).toBool();
  QString m_otherUrl = settings.value("otherUrl","").toString();
  QString m_myCall=settings.value("MyCall","").toString();
  QString m_myGrid=settings.value("MyGrid","").toString();
  QString theDate = guiDate; // "2025 Mar 26";

  QString theUrl;

  if(m_w3szUrl)
  {
    theUrl = w3szUrlAddr;
  }
  else
  {
    theUrl = m_otherUrl;
  }

  QNetworkAccessManager *manager = new QNetworkAccessManager(this);
  QUrl url(theUrl);
  QNetworkRequest request(url);
  request.setRawHeader("User-Agent", "QMAP v0.5");
  request.setRawHeader("X-Custom-User-Agent", "QMAP v0.5");
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

  for (const QString &theLine : decodeList) {
    QStringList thePostLine = theLine.split(" ",SkipEmptyParts);
    if((thePostLine.at(5) == "CQ" || thePostLine.at(5) == "QRZ" || thePostLine.at(5) == "CQV" ||  thePostLine.at(5) == "CQH" || thePostLine.at(5) == "QRT") && m_myCall.length() >=3 && m_myGrid.length()>=4) {
      QString freq = guiFreq + "." + thePostLine.at(0).trimmed();
      QString dF = thePostLine.at(1).trimmed();
      QString rpol = thePostLine.at(2).trimmed();
      QString utcdatetimestringOriginal = guiDate + " " + thePostLine.at(3).trimmed() + "00"; //needs 2 spaces between date and time
      QDateTime utcdatetimeUTC = QDateTime::fromString(utcdatetimestringOriginal, "yyyy MMM dd  HHmmss");
      utcdatetimeUTC.setTimeSpec((Qt::UTC));
      QString utcdatetimeUTCString = utcdatetimeUTC.toString("yyyy-MM-ddTHH:mm:ss");
      utcdatetimeUTCString = utcdatetimeUTCString + "Z";
      QString dB = thePostLine.at(4).trimmed();
      QString msgType = thePostLine.at(5).trimmed().toUpper();
      QString callsign = thePostLine.at(6).trimmed().toUpper();
      QString grid = thePostLine.at(7).trimmed();
      QString modeChar = thePostLine.at(9).trimmed();
      QString mode;
      if(modeChar=="0") {
        modeChar = thePostLine.at(10).trimmed();
        if(modeChar.contains("#")) mode = "JT65" + modeChar.back();
      }
      else if(modeChar.contains(":")) mode = "Q65-60" + modeChar.back();
      else mode = theLine.right(5);
      QString dT =thePostLine.at(8).trimmed();

      QString postString =  "skedfreq=" + freq + "&rxfreq=" + dF + "&rpol=" + rpol + "&dt="  +  dT + "&dB="  + dB + "&msgtype="  +  msgType.toUpper() + "&callsign="  +  callsign.toUpper() + "&grid="  +  grid.toUpper() + "&mode="  +  mode + "&utcdatetime="  +  utcdatetimeUTCString + "&spotter="  +  m_myCall.toUpper() + "&spottergrid="  + m_myGrid.toUpper() + "&apptype=MAP65";

      QByteArray postByteArray = postString.toUtf8();
      request.setRawHeader("Content-Length",QByteArray::number(postByteArray.size()));

      try {

        QNetworkReply *reply = manager->post(request,postByteArray);

        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (!reply->error()) {
          QByteArray responseData = reply->readAll();
          qDebug() << reply->readAll();
        }
        else {
          qDebug() << reply->errorString();
        }
      }
      catch(...)
      {

      }
    }
  }
}

void Messages::setText(QString t, QString t2)
{
  QString cfreq,cfreq0;
  m_t=t;
  m_t2=t2;

  //liveCQ
  QStringList cqliveText;
  doLiveCQ = true;

  QString s="QTextBrowser{background-color: "+m_colorBackground+"}";
  ui->messagesTextBrowser->setStyleSheet(s);

  ui->messagesTextBrowser->clear();
  QStringList lines = t.split( "\n", SkipEmptyParts );
  foreach( QString line, lines ) {
    QString t1=line.mid(0,75); //liveCQ
    int ncq=t1.indexOf(" CQ ");
    if((m_cqOnly or m_cqStarOnly) and  ncq< 0) continue;
    if(m_cqStarOnly) {
      QString caller=t1.mid(ncq+4,-1);
      int nz=caller.indexOf(" ");
      caller=caller.mid(0,nz);
      int i=t2.indexOf(caller);
      if(t2.mid(i-1,1)==" ") continue;
    }
    int n=line.mid(50,2).toInt();
//    if(line.indexOf(":")>0) n=-1;
//    if(n==-1) ui->messagesTextBrowser->setTextColor("#ffffff");  // white
    if(n==0) ui->messagesTextBrowser->setTextColor(m_color0);
    if(n==1) ui->messagesTextBrowser->setTextColor(m_color1);
    if(n==2) ui->messagesTextBrowser->setTextColor(m_color2);
    if(n>=3) ui->messagesTextBrowser->setTextColor(m_color3);
    cfreq=t1.mid(0,3);
    if(cfreq == cfreq0) {
      t1="   " + t1.mid(3,-1);
    }
    cfreq0=cfreq;
    ui->messagesTextBrowser->append(t1.mid(0,50)); //liveCQ
    QString livecqStr = t1.mid(0,48) + t1.mid(51,t1.length()-51); //liveCQ
    cqliveText.append(livecqStr);  //liveCQ

  }

  if(doLiveCQ) {
    if(cqliveText.size() != 0) {
      sendLiveCQData(cqliveText);  //liveCQ
      doLiveCQ = false;
    }
  }

}

void Messages::selectCallsign2(bool ctrl)
{
  QString t = ui->messagesTextBrowser->toPlainText();   //Full contents
  int i=ui->messagesTextBrowser->textCursor().position();
  int i0=t.lastIndexOf(" ",i);
  int i1=t.indexOf(" ",i);
  QString hiscall=t.mid(i0+1,i1-i0-1);
  if(hiscall!="") {
    if(hiscall.length() < 13) {
      QString t1 = t.mid(0,i);              //contents up to text cursor
      int i1=t1.lastIndexOf("\n") + 1;
      QString t2 = t.mid(i1,-1);            //selected line to end
      int i2=t2.indexOf("\n");
      t2=t2.left(i2);                       //selected line
      emit click2OnCallsign(hiscall,t2,ctrl);
    }
  }
}

void Messages::setColors(QString t)
{
  m_colorBackground = "#"+t.mid(0,6);
  m_color0 = "#"+t.mid(6,6);
  m_color1 = "#"+t.mid(12,6);
  m_color2 = "#"+t.mid(18,6);
  m_color3 = "#"+t.mid(24,6);
  setText(m_t,m_t2);
}

void Messages::on_cbCQ_toggled(bool checked)
{
  m_cqOnly = checked;
  setText(m_t,m_t2);
}

void Messages::on_cbCQstar_toggled(bool checked)
{
  m_cqStarOnly = checked;
  setText(m_t,m_t2);
}
