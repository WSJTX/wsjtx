#include "messages.h"
#include <QSettings>
#include "SettingsGroup.hpp"
#include "ui_messages.h"
#include "mainwindow.h"
#include "qt_helpers.hpp"
#include "../revision_utils.hpp"
#include "../Logger.hpp"
#include "PSKReporter.hpp"
#include "liveCQSender.hpp"

#include <QCoreApplication> //liveCQ
#include <QNetworkAccessManager> //liveCQ
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QEventLoop>
#include <QDateTime>
#include <QString>
#include <QThread>
#include <QDebug>

#include <iostream>
#include <string>
#include <memory>

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
  
  QSettings settings2 {m_settings_filename, QSettings::IniFormat};
  SettingsGroup h {&settings2, "Common"};
  bool m_w3szUrl = settings2.value("w3szUrl",true).toBool();
  QString m_otherUrl = settings2.value("otherUrl","").toString();
  QString m_myCall=settings2.value("MyCall","").toString();
  QString m_myGrid=settings2.value("MyGrid","").toString();
  QString theUrl;
  
  if(m_w3szUrl) {
    theUrl = w3szUrlAddr;
  } else {
    theUrl = m_otherUrl;
  }
  
  m_cqOnly=false;
  m_cqStarOnly=false;
  QString guiDate;
  QStringList allDecodes =  { "" };
  QStringList allDecodes2 = { "" };
  connect (ui->messagesTextBrowser, &DisplayText::selectCallsign, this, &Messages::selectCallsign2);
  
    // Create the thread and your liveCQ object
  livecqThread = new QThread(this);
  
  connect(livecqThread, &QThread::started, this, [this, m_myCall, m_myGrid, theUrl]() {
    auto* reporter2 = new liveCQSender(m_myCall, m_myGrid, theUrl);
    reporter2->moveToThread(this->livecqThread);      

    connect(reporter2, &liveCQSender::destroyed, livecqThread, &QThread::quit);
    QMetaObject::invokeMethod(reporter2, "init", Qt::QueuedConnection);
    
    // Connect signals for control and communication
    connect(this, &Messages::sendRemoteStationData2, reporter2, &liveCQSender::addRemoteStation);
  
});
  connect(livecqThread, &QThread::finished, livecqThread, &QObject::deleteLater);
  livecqThread->start();  
  
    // Create the thread and your PSKReporter object
  pskThread = new QThread(this);
  
  connect(pskThread, &QThread::started, this, [this, m_myCall,m_myGrid]() {
      auto* reporter = new PSKReporter(m_myCall, m_myGrid, QString {"MAP65 v"
              + QCoreApplication::applicationVersion ()
  + " " + revision ()}.simplified () + " improved PLUS");

  reporter->moveToThread(this->pskThread);
  connect(reporter, &PSKReporter::destroyed, pskThread, &QThread::quit);
  QMetaObject::invokeMethod(reporter, "init", Qt::QueuedConnection);
  
  // Connect signals for control and communication
  connect(this, &Messages::sendLocalStationData, reporter, &PSKReporter::setLocalStation);
  connect(this, &Messages::sendRemoteStationData, reporter, &PSKReporter::addRemoteStation);
    
  });
    connect(pskThread, &QThread::finished, pskThread, &QObject::deleteLater);
    pskThread->start(); 
    if (m_spot_to_psk_reporter) {   
      initializePSKReporting();
    }
}
 
Messages::~Messages()
{
  //QSettings settings {m_settings_filename, QSettings::IniFormat};
  //SettingsGroup g {&settings, "MainWindow"};
  //settings.setValue ("MessagesGeom", geometry ());
  delete ui;
}

void Messages::closeEvent(QCloseEvent *event)
{
  if (!m_closingForShutdown) {
      hide();
      event->ignore(); // Don't close, just hide
      return;
  }

  // app shutdown
  QSettings settings {m_settings_filename, QSettings::IniFormat};
  SettingsGroup g {&settings, "MainWindow"};
  settings.setValue ("MessagesGeom", geometry ());
  settings.sync(); // Ensure data is written to disk 
  event->accept(); // Allow destruction 
}   

void Messages::initializePSKReporting()
{  
  QSettings settings {m_settings_filename, QSettings::IniFormat};
  SettingsGroup g {&settings, "Common"}; 
  QString receiverCallsign=settings.value("MyCall","").toString();
  QString receiverLocator=settings.value("MyGrid","").toString();
  emit sendLocalStationData(receiverCallsign, receiverLocator, "N/A", "N/A (MAP65)");   
}

void Messages::sendLiveCQData(QStringList decodeList) {
  QSettings settings(m_settings_filename, QSettings::IniFormat);
  SettingsGroup g {&settings, "Common"};
  bool m_w3szUrl = settings.value("w3szUrl",true).toBool();
  QString m_otherUrl = settings.value("otherUrl","").toString();
  QString m_myCall=settings.value("MyCall","").toString();
  QString m_myGrid=settings.value("MyGrid","").toString();
  bool m_xpol = settings.value("Xpol",false).toBool();
  QString rpol = "--";
  QString theUrl;

  if(w3szUrlAddr.contains("https://") && m_w3szUrl) {
    theUrl = w3szUrlAddr;
  } else if (m_otherUrl.contains("https://") && !m_w3szUrl) { 
    theUrl = m_otherUrl;
  }
  else return;
  for (const QString &theLine : decodeList) {
    QStringList thePostLine = theLine.split(" ",SkipEmptyParts);
    if((thePostLine.at(5) == "CQ" || thePostLine.at(5) == "QRZ" || thePostLine.at(5) == "CQV" ||  thePostLine.at(5) == "CQH" || thePostLine.at(5) == "QRT") && m_myCall.length() >=3 && m_myGrid.length()>=4) {
      if(allDecodes.filter(theLine.mid(0,53)).length() == 0) {
        allDecodes.append(theLine);
        QString freq = thePostLine.at(0).trimmed();
        QString dF = thePostLine.at(1).trimmed();
        QString utcdatetimestringOriginal = guiDate + " " + thePostLine.at(3).trimmed() + "00"; //needs 2 spaces between date and time
        QDateTime utcdatetimeUTC = QDateTime::fromString(utcdatetimestringOriginal, "yyyy MMM dd  HHmmss");
        utcdatetimeUTC.setTimeSpec((Qt::UTC));
        QString utcdatetimeUTCString = utcdatetimeUTC.toString("yyyy-MM-ddTHH:mm:ss");
        utcdatetimeUTCString = utcdatetimeUTCString + "Z";
        QString dB = thePostLine.at(4).trimmed();
        QString msgType = thePostLine.at(5).trimmed().toUpper();
        QString callsign = "";
        QString grid = "--";
        QString mode="";
        QString txpol = " ";
        QString dT = "";
        QString modeChar = "";
        // Handle CQ CALL but NO GRID -- dot at 7
      if(thePostLine.at(7).contains(".")) {
          callsign = thePostLine.at(6).trimmed().toUpper();
          bool isCall = testCall(callsign);
          if(!isCall) continue;
          dT =thePostLine.at(7).trimmed();
          modeChar = thePostLine.at(8).trimmed(); 
          if(modeChar.contains("#")) mode = "JT65" + modeChar.back();
          else if(modeChar.contains(":")) mode = "Q65-60" + modeChar.back();          
          if(m_xpol) {
            rpol = thePostLine.at(2).trimmed();
          } else {
            rpol = "--";
          }
          txpol = "--";  
          
        // Handle CQ CALL GRID or CQ XXX CALL -- dot at 8
        } else if (thePostLine.at(8).contains(".")) {
          // Test for callsign at thePostLine(6)
          callsign = thePostLine.at(6).trimmed().toUpper();
          bool isCall = testCall(callsign);
          if(isCall) {
            grid = thePostLine.at(7).trimmed();  
            // Handle CQ XXX CALL
          } else {
            callsign = thePostLine.at(7).trimmed().toUpper();
            bool isCall = testCall(callsign);
            if(!isCall) continue;
          }          
          dT =thePostLine.at(8).trimmed();
          modeChar = thePostLine.at(9).trimmed();
          if(modeChar.contains("#")) 
          {  
            mode = "JT65" + modeChar.back();            
            if (m_xpol) {
              rpol = thePostLine.at(2).trimmed();
              if(thePostLine.length()==11) {
                if(thePostLine.at(10).contains("H")) txpol = "H";
                else if(thePostLine.at(10).contains("V")) txpol = "V";
                else txpol+"--";
              } else txpol="--";
            }
          } else if(modeChar.contains(":")) {
            mode = "Q65-60" + modeChar.back();            
            if (m_xpol) {
              rpol = thePostLine.at(2).trimmed();
              if(thePostLine.length()==11) {
              if(thePostLine.at(10).contains("H")) txpol = "H";
              else if(thePostLine.at(10).contains("V")) txpol = "V";
              else txpol="--";
            } else txpol="--";
          }
        }
        // Handle CQ XXX CALL GRID
        }  else if(thePostLine.at(9).contains(".")) {
            callsign = thePostLine.at(7).trimmed().toUpper();
            bool isCall = testCall(callsign);
            if(!isCall) continue;
            grid = thePostLine.at(8).trimmed();  
             
            dT =thePostLine.at(9).trimmed();
            modeChar = thePostLine.at(10).trimmed();
            if(modeChar.contains("#")) 
            {  
              mode = "JT65" + modeChar.back();            
              if (m_xpol) {
                rpol = thePostLine.at(2).trimmed();
                if(thePostLine.length()==12) {
                  if(thePostLine.at(11).contains("H")) txpol = "H";
                  else if(thePostLine.at(11).contains("V")) txpol = "V";
                  else txpol="--";
                } else txpol="--";                
              }
            } else if(modeChar.contains(":")) {
              mode = "Q65-60" + modeChar.back();            
              if (m_xpol) {
                rpol = thePostLine.at(2).trimmed();
                if(thePostLine.length()==12) {
                  if(thePostLine.at(11).contains("H")) txpol = "H";
                  else if(thePostLine.at(11).contains("V")) txpol = "V";
                  else txpol="--";
                } else txpol="--";  
              }                
            } 
        }
        else {
          continue; 
        }
        if(mode.contains("JT65") || mode.contains("Q65")) {
          QString postString =  "skedfreq=" + freq + "&rxfreq=" + dF + "&rpol=" + rpol + "&dt="  +  dT + "&dB="  + dB + "&msgtype="  +  msgType.toUpper() + "&callsign="  +  callsign.toUpper() + "&grid="  +  grid.toUpper() + "&mode="  +  mode + "&utcdatetime="  +  utcdatetimeUTCString + "&spotter="  +  m_myCall.toUpper() + "&spottergrid="  + m_myGrid.toUpper() + "&txpol=" + txpol + "&apptype=MAP65";
          QByteArray postByteArray = postString.toUtf8();
          
        emit sendRemoteStationData2(postByteArray, theUrl);
        }
      }
    }
  }
}

bool Messages::testCall(QString w)
{
// Check "callsign" to see if it could be a valid standard callsign or a valid
// compound callsign.
// Return a logical "call ok" indicator.
  if(w.indexOf('.') >= 0) return false;
  if(w.indexOf('+') >= 0) return false;
  if(w.indexOf('-') >= 0) return false;
  if(w.indexOf('?') >= 0) return false;
  w = w.replace('<',"");
  w = w.replace('>',""); 
  int n1=w.length();
  if(n1 > 11) return false;
  QString bc = QString();
  QStringList wSplit = w.split("/");
  if(wSplit.length() > 1) {
    if(wSplit.at(0).length() > wSplit.at(1).length()) {
      bc = wSplit.at(0);
    }
    else {
      bc = wSplit.at(1);
    }
  }
  else {
    bc = w;
  }
  int nbc=bc.trimmed().length();
  if(nbc > 8) return false;  //Base call should have no more than 8 characters  e.g. YW18FIFA

// One of first two characters must be a letter
  if((!bc[0].isLetter()) && (!bc[1].isLetter())) return false;
// Real calls don't start with Q, but we'll allow the placeholder
// callsign QU1RK to be considered a standard call:
  if(bc[0]=='Q' && bc.mid(0,5) != "QU1RK") return false;

// Must have a digit in 2nd or 3rd or 4th position
  int i1=0;
  if(bc[1].isDigit()) i1=1;
  if(bc[2].isDigit()) i1=2;
  if(bc[3].isDigit()) i1=3;
  if(i1==0) return false;

// Callsign must have a suffix of 1-4 letters e.g. YW18FIFA
  if(i1==nbc) return false;
  int n=0;
  QChar j=QChar();
  for (int i=i1+1; i<=nbc-1; ++i) {
     j=bc[i];
     if(j<QChar('A') || j > QChar('Z')) return false;
     n=n+1;
  }
  if(n >= 1 && n <= 4) return true;
  
  return false;  
}


void Messages::onFinished(QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError) {
		qDebug() << "Reply in messages::inFinished is: " << reply->readAll();
    } else {
		qDebug() << "Error message in messages::inFinished is: " <<  reply->errorString();
    }
    reply->deleteLater();
}

void Messages::setText(QString t, QString t2)
{
  QString cfreq,cfreq0;
  m_t=t;
  m_t2=t2;
  //bool firstTime = true;

  QStringList cqliveText;  //liveCQ
  doLiveCQ = true;         //liveCQ

  QString s="QTextBrowser{background-color: "+m_colorBackground+"}";
  ui->messagesTextBrowser->setStyleSheet(s);

  ui->messagesTextBrowser->clear();
  QStringList lines = t.split( "\n", SkipEmptyParts );
  foreach( QString line, lines ) {
    QString t1=line.mid(0,81); //was 0,75
    int ncq=t1.indexOf(" CQ ");
    if((m_cqOnly or m_cqStarOnly) and  ncq< 0) continue;
    if(m_cqStarOnly) {
      QString caller=t1.mid(ncq+4,-1);
      int nz=caller.indexOf(" ");
      caller=caller.mid(0,nz);
      int i=t2.indexOf(caller);
      if(t2.mid(i-1,1)==" ") continue;
    }
    int n=line.mid(61,2).toInt();  //was 55,2
//    if(line.indexOf(":")>0) n=-1;
//    if(n==-1) ui->messagesTextBrowser->setTextColor("#ffffff");  // white
    if(n==0) ui->messagesTextBrowser->setTextColor(m_color0);
    if(n==1) ui->messagesTextBrowser->setTextColor(m_color1);
    if(n==2) ui->messagesTextBrowser->setTextColor(m_color2);
    if(n>=3) ui->messagesTextBrowser->setTextColor(m_color3);
    QString livecqStr = t1.mid(0,59) + t1.mid(62,t1.length()-62) + " " + t1.mid(60,2); // was 53,56,56,54
    if(cqliveText.filter(livecqStr.mid(0,59)).length()==0) cqliveText.append(livecqStr); // was 0,53
    cfreq=t1.mid(5,3);
    if(cfreq == cfreq0) {
      t1="        " + t1.mid(8,-1);
    }
    cfreq0=cfreq;
    ui->messagesTextBrowser->append(t1.mid(5,67)); //was 5,61
  }
  if(doLiveCQ) {                      //liveCQ
    if(cqliveText.size() > 0) {       //liveCQ
      sendLiveCQData(cqliveText);     //liveCQ
      doLiveCQ = false;               //liveCQ
    }                                 //liveCQ
  }  //liveCQ
  if (m_spot_to_psk_reporter) {
      sendPSKReporterData(cqliveText); //PSKReporter
  }
}

void Messages::sendPSKReporterData(QStringList decodeList) {  
    
  QSettings settings(m_settings_filename, QSettings::IniFormat);
  SettingsGroup g {&settings, "Common"};
  
  //QRZ parameters (3)
  QString receiverCallsign=settings.value("MyCall","").toString();
  QString receiverLocator=settings.value("MyGrid","").toString();
  m_spot_to_psk_reporter = settings.value("spotPSK",true).toBool();
  //QRZ parameters (8)
  QString senderCallsign;
  QString senderLocator;
  double doubleFreq = 0.0; //(Hz)
  qint64 frequency = 0.0; 
  int sNR = -10;
  QString mode = ""; 
  bool ok = false;
  
  for (const QString &theLine : decodeList) {
    QStringList thePostLine = theLine.split(" ",SkipEmptyParts);
    if((thePostLine.at(5) == "CQ" || thePostLine.at(5) == "QRZ" || thePostLine.at(5) == "CQV" ||  thePostLine.at(5) == "CQH" || thePostLine.at(5) == "QRT") && receiverCallsign.length() >=3 && receiverLocator.length()>=4) {
      if(allDecodes2.filter(theLine.mid(0,53)).length() == 0) {
        allDecodes2.append(theLine);
        QString freq = thePostLine.at(0).trimmed();
        doubleFreq = freq.toDouble(&ok);
        frequency = qRound64(doubleFreq * 1000000);
        QString dF = thePostLine.at(1).trimmed();
        QString dB = thePostLine.at(4).trimmed();
        sNR = dB.toInt(&ok);
        QString msgType = thePostLine.at(5).trimmed().toUpper();
        senderCallsign = "";
        senderLocator = "--";
        mode="";
        sNR=-10;
        QString modeChar = "";     

        int m_TRperiod = 60;
        QString sTimeString = (thePostLine.at(3).trimmed() + "00");
        int sTime = sTimeString.toInt();
        int h=sTimeString.mid(0,2).toInt();
        int m=sTimeString.mid(2,2).toInt();
        int s=sTimeString.mid(4,2).toInt();
        QTime time2(h, m, s);
        QDateTime qSpotTime;
        if (sTime + m_TRperiod < 236000) {
          qSpotTime = QDateTime(QDateTime::currentDateTimeUtc().date(), time2, Qt::UTC); 
        }
        else {
          qSpotTime = QDateTime((QDateTime::currentDateTimeUtc().addDays(-1)).date(), time2, Qt::UTC); 
        }            
        
        // Handle CQ CALL but NO GRID -- dot at 7
      if(thePostLine.at(7).contains(".")) {
          senderCallsign = thePostLine.at(6).trimmed().toUpper();
          bool isCall = testCall(senderCallsign);
          if(!isCall) continue;
          modeChar = thePostLine.at(8).trimmed(); 
          if(modeChar.contains("#")) mode = "JT65" + modeChar.back();
          else if(modeChar.contains(":")) mode = "Q65-60" + modeChar.back();   
          
        // Handle CQ CALL GRID or CQ XXX CALL -- dot at 8
        } else if (thePostLine.at(8).contains(".")) {
          // Test for callsign at thePostLine(6)
          senderCallsign = thePostLine.at(6).trimmed().toUpper();
          bool isCall = testCall(senderCallsign);
          if(isCall) {
            senderLocator = thePostLine.at(7).trimmed();  
            // Handle CQ XXX CALL
          } else {
            senderCallsign = thePostLine.at(7).trimmed().toUpper();
            bool isCall = testCall(senderCallsign);
            if(!isCall) continue;
          }          
          modeChar = thePostLine.at(9).trimmed();
          if(modeChar.contains("#")) 
          {  
            mode = "JT65" + modeChar.back();   
          } else if(modeChar.contains(":")) {
            mode = "Q65-60" + modeChar.back();      
          }
        // Handle CQ XXX CALL GRID
        }  else if(thePostLine.at(9).contains(".")) {
            senderCallsign = thePostLine.at(7).trimmed().toUpper();
            bool isCall = testCall(senderCallsign);
            if(!isCall) continue;
            senderLocator = thePostLine.at(8).trimmed();  
             
            modeChar = thePostLine.at(10).trimmed();
            if(modeChar.contains("#")) 
            {  
              mode = "JT65" + modeChar.back();      
            } else if(modeChar.contains(":")) {
              mode = "Q65-60" + modeChar.back();     
            } 
        }
        else {
          continue; 
        }             
                
        emit sendRemoteStationData(senderCallsign, senderLocator, frequency, mode, sNR, qSpotTime);        
      }
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
