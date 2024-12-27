#include <QApplication>
#include <QSettings>
#include <QCloseEvent>
#include <QDateTime>
#include "SettingsGroup.hpp"
#include "Configuration.hpp"
#include "QSYMessage.h"
#include "ui_QSYMessage.h"

QSYMessage::QSYMessage(const QString& message,const QString& theCall, QSettings * settings, Configuration const * configuration, QWidget *parent) :
  QWidget {parent},
  settings_ {settings},
  configuration_ {configuration},
  ui(new Ui::QSYMessage),
  receivedMessage(message),receivedCall(theCall)
{
  ui->setupUi(this);
  read_settings();
  setWindowTitle ("Message" + QDateTime::currentDateTimeUtc().toString(" [hh:mm:ss]"));
  ui->label->setStyleSheet("font: bold; font-size: 30pt");
  getBandModeFreq();
}

QSYMessage::~QSYMessage()
{
  delete ui;
}

void QSYMessage::closeEvent (QCloseEvent * e) {
  write_settings();
  e->accept();
}

void QSYMessage::read_settings () {
  SettingsGroup g (settings_, "QSYMessage");
  move (settings_->value ("window/pos", pos()).toPoint());
}

void QSYMessage::write_settings () {
  SettingsGroup g (settings_, "QSYMessage");
  settings_->setValue ("window/pos", pos());
}

void QSYMessage::on_yesButton_clicked()
{
  QString message = QString(Radio::base_callsign(receivedCall)) + QString(".OKQSY");
  Q_EMIT sendReply(message);
  ui->yesButton->setStyleSheet("background-color:#00ff00");
  ui->noButton->setStyleSheet("background-color:palette(button).color()");
}

void QSYMessage::on_noButton_clicked()
{
  QString message = QString(Radio::base_callsign(receivedCall)) + QString(".NOQSY");
  Q_EMIT sendReply(message);
  ui->noButton->setStyleSheet("background-color:red; color:white");
  ui->yesButton->setStyleSheet("background-color:palette(button).color()");
}

void QSYMessage::getBandModeFreq()
{
  if(receivedMessage.at(0) =='$' && receivedMessage.size() >= 5)
  {
#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
    QStringList bhList = receivedMessage.split(" ",Qt::SkipEmptyParts);
#else
    QStringList bhList = receivedMessage.split(" ",QString::SkipEmptyParts);
#endif
    ui->label->setText(bhList[1] + " replied " +bhList[2].mid(0,2));
    ui->yesButton->hide();
    ui->noButton->hide();
  }
  else if(receivedMessage.at(0) =='Z' && receivedMessage.at(1) =='A' && receivedMessage.size() >= 5) {
      QString messageContent = "";
      QString text1 = "";
      QString msgNum =receivedMessage.mid(2,3);
      if(msgNum == "001") {
          text1 = "You are in the log";
      } else if(msgNum =="002") {
          text1 = "Please call me by phone";
      } else if(msgNum =="003") {
          text1 = "Check your email";
      } else if(msgNum =="004") {
          text1 = "You are not in Contest Mode";
      } else if(msgNum =="005") {
          text1 = "Please check your PC Clock";
      } else if(msgNum =="006") {
          text1 = "You are transmitting in the wrong time slot";
      } else if(msgNum =="007") {
          text1 = "Please transmit above 1000 Hz";
      } else if(msgNum =="008") {
          text1 = "Your transmitter is overmodulated";
      } else if(msgNum =="009") {
          text1 = "Your audio is distorted";
      } else if(msgNum =="010") {
          text1 = "You are transmitting on top of a rare DX station";
      } else if(msgNum =="011") {
          text1 = "Please check HB9Q Logger";
      } else if(msgNum =="012") {
          text1 = "Please check Ping Jockey";
      } else if(msgNum =="013") {
          text1 = "Please check ON4KST";
      } else if(msgNum =="014") {
          text1 = "Please QSL via LoTW";
      }
      ui->noButton->setHidden(true);
      ui->yesButton->setText("OK");
      ui->label->setText(text1);
  }
  else if(receivedMessage.at(0).isLetter() || receivedMessage.at(0) == '9' || receivedMessage.at(0) == '4' || receivedMessage.at(0) == '7')
  {
    QString bandParam = "";
    QString modeParam = receivedMessage.at(0);
    QString freqParam = "";
    bandParam = receivedMessage.mid(0,1);
    modeParam = receivedMessage.at(1);
    freqParam = receivedMessage.mid(2, 3);
    QString band = "";
    QString mode = "";
    QString freq = "";

    if (bandParam ==  "A") {
      if (modeParam.at(0).isLetter()) {
        freq = "50.";
      } else {
        if (modeParam =="0") {
          freq = "50.";
        } else if (modeParam =="1") {
          freq = "51.";
        } else if (modeParam =="2") {
          freq = "52.";
        } else if (modeParam =="3") {
          freq = "53.";
        }
			}
		}
    else if (bandParam ==  "B") {
      if (modeParam.at(0).isLetter()) {
        freq = "144.";
      } else {
        if (modeParam =="4") {
          freq = "144.";
        } else if (modeParam =="5") {
          freq = "145.";
        } else if (modeParam =="6") {
          freq = "146.";
        } else if (modeParam =="7") {
          freq = "147.";
        }
			}
		}
    else if (bandParam ==  "C") {
      if (modeParam.at(0).isLetter()) {
        freq = "222.";
      } else {
        if (modeParam =="2") {
          freq = "222.";
        } else if (modeParam =="3") {
          freq = "223.";
        }
			}
		}
    else if (bandParam ==  "D") {
      if (modeParam.at(0).isLetter()) {
        freq = "432.";
      } else {
        if (configuration_->region()==2) {
          if (modeParam =="0") {
            freq = "440.";
          } else if (modeParam =="1") {
            freq = "441.";
          } else if (modeParam =="2") {
            freq = "442.";
          } else if (modeParam =="3") {
            freq = "443.";
          } else if (modeParam =="4") {
            freq = "444.";
          } else if (modeParam =="5") {
            freq = "445.";
          } else if (modeParam =="6") {
            freq = "446.";
          } else if (modeParam =="7") {
            freq = "447.";
          } else if (modeParam =="8") {
            freq = "448.";
          } else if (modeParam =="9") {
            freq = "449.";
          }
        } else { //not region 2
          if (modeParam =="0") {
            freq = "430.";
          } else if (modeParam =="1") {
            freq = "431.";
          } else if (modeParam =="2") {
            freq = "432.";
          } else if (modeParam =="3") {
            freq = "433.";
          } else if (modeParam =="4") {
            freq = "434.";
          } else if (modeParam =="5") {
            freq = "435.";
          } else if (modeParam =="6") {
            freq = "436.";
          } else if (modeParam =="7") {
            freq = "437.";
          } else if (modeParam =="8") {
            freq = "438.";
          } else if (modeParam =="9") {
            freq = "439.";
          }
        }
      }
    }
    else if (bandParam ==  "E") {
      if (modeParam.at(0).isLetter()) {
        freq = "1296.";
      } else {
        if (modeParam =="0") {
          freq = "1290.";
        } else if (modeParam =="1") {
          freq = "1291.";
        } else if (modeParam =="2") {
          freq = "1292.";
        } else if (modeParam =="3") {
          freq = "1293.";
        } else if (modeParam =="4") {
          freq = "1294.";
        } else if (modeParam =="5") {
          freq = "1295.";
        } else if (modeParam =="6") {
          freq = "1296.";
        } else if (modeParam =="7") {
          freq = "1297.";
        } else if (modeParam =="8") {
          freq = "1298.";
        } else if (modeParam =="9") {
          freq = "1299.";
        }
      }
    }
    else if (bandParam ==  "F") {
      freq = "2304.";
    }
    else if (bandParam ==  "G") {
      freq = "3400.";
    }
    else if (bandParam ==  "H") {
      freq = "5760.";
    }
    else if (bandParam ==  "I") {
      freq = "10368.";
    }
    else if (bandParam ==  "J") {
        if (configuration_->region()==2) {
            freq = "24192.";
        } else {
            freq = "24048.";
        }
    }
    else if (bandParam ==  "9") {
      freq = "902.";
    }
    else if (bandParam ==  "K") {
      freq = "903.";
    }
    else if (bandParam ==  "J") {
      freq = "24192.";
    }
    else if (bandParam ==  "X") {
        freq = "24048.";
    }
    else if (bandParam ==  "4") {
        freq = "40.";
    }
    else if (bandParam ==  "7") {
        freq = "70.";
    }
    else if (bandParam ==  "L") {
      freq = "0.";
    }
    else if (bandParam ==  "M") {
      freq = "1.";
    }
    else if (bandParam ==  "N") {
      freq = "3.";
    }
    else if (bandParam ==  "O") {
      freq = "5.";
    }
    else if (bandParam ==  "P") {
      freq = "7.";
    }
    else if (bandParam ==  "Q") {
      freq = "10.";
    }
    else if (bandParam ==  "R") {
      freq = "14.";
    }
    else if (bandParam ==  "S") {
      freq = "18.";
    }
    else if (bandParam ==  "T") {
      freq = "21.";
    }
    else if (bandParam ==  "U") {
      freq = "24.";
    }
    else if (bandParam ==  "V") {
      freq = "28.";
    }
    else if (bandParam ==  "W") {
      freq = "29.";
    } else {
      freq = "";
    }

    freq = freq + freqParam;
    if(!modeParam.at(0).isLetter()) {
			mode = "FM";
		}
    else if (modeParam ==  'V') {
      mode = "SSB";
    }
    else if (modeParam == 'J') {
      mode = "FT4";
    }
    else if (modeParam ==  'L') {
      mode = "FT8";
    }
    else if (modeParam ==  'K') {
        mode = "MSK144";
    }
    else if (modeParam ==  'W') {
      mode = "CW";
    }
    else if (modeParam ==  'A') {
      mode = "JT9";
    }
    else if (modeParam ==  'B') {
      mode = "JT65";
    }
    else if (modeParam ==  'C') {
      mode = "FST4";
    }
    else if (modeParam ==  'D') {
      mode = "Q65-30B";
    }
    else if (modeParam ==  'E') {
      mode = "Q65-60C";
    }
    else if (modeParam ==  'F') {
      mode = "Q65-60D";
    }
    else if (modeParam ==  'G') {
      mode = "Q65-60E";
    }
    else if (modeParam ==  'H') {
      mode = "Q65-120D";
    }
    else {
      mode = "";
    }
    ui->label->setText("QSY to\n" + freq + " MHz\n" + "mode " + mode);
  }
}
