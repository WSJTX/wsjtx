#include "qsymonitor.h"
#include "ui_qsymonitor.h"
#include "SettingsGroup.hpp"
#include "Configuration.hpp"
#include "qt_helpers.hpp"
#include "commons.h"
#include <QSettings>
#include <QObject>
#include <QCloseEvent>
#include <QThread>
#include <QLabel>
#include <QWidget>
#include <QMessageBox>
#include <QStringList>

QSYMonitor::QSYMonitor(QSettings * settings, QFont const& font, Configuration const * configuration, QWidget *parent)
  : QWidget(parent),
  settings_ {settings},
  configuration_ {configuration},
  ui(new Ui::QSYMonitor)
{
  ui->setupUi(this);
  setWindowTitle (QApplication::applicationName () + " - " + tr ("QSY Monitor"));
  ui->QSYMonitorTextBrowser->setReadOnly (true);
  changeFont (font);
  read_settings ();
  ui->qsyMonitorLabel->setText("  UTC    Call      Freq        Mode");
}

QSYMonitor::~QSYMonitor()
{
  delete ui;
}

void QSYMonitor::getQSYData(QString value)
{
  createQSYLine(value);
}

void QSYMonitor::read_settings ()
{
  SettingsGroup g (settings_, "QSYMonitor");
  move (settings_->value ("window/pos", pos ()).toPoint ());
  restoreGeometry(settings_->value("geometry").toByteArray());
}

void QSYMonitor::write_settings ()
{
  SettingsGroup g (settings_, "QSYMonitor");
  settings_->setValue ("window/pos", pos ());
  settings_->setValue ("geometry", saveGeometry());
}

void QSYMonitor::changeFont (QFont const& font)
{
  ui->qsyMonitorLabel->setStyleSheet (font_as_stylesheet (font));
  ui->QSYMonitorTextBrowser->setStyleSheet (font_as_stylesheet (font));
  updateGeometry ();
}

void QSYMonitor::closeEvent (QCloseEvent * e)
{
  write_settings();
  e->accept();                 // was ignore
}

void QSYMonitor::on_clearButton_clicked()
{
  ui->QSYMonitorTextBrowser->clear();
}

void QSYMonitor::createQSYLine(QString value)
{
  QStringList qsySpot = value.split(' ');
  if(qsySpot.length() == 3) {
    QString theTime = qsySpot[0];
    QString theCall = qsySpot[1];
    getBandModeFreq(theTime, theCall, qsySpot[2]);
  }
}

void QSYMonitor::getBandModeFreq(QString theTime, QString theCall, QString value)
{
  if(value.at(0).isLetter() || value.at(0) == '9' || value.at(0) == '4' || value.at(0) == '7') {
    QString bandParam = "";
    QString modeParam = value.at(0);
    QString freqParam = "";
    bandParam = value.mid(0,1);
    modeParam = value.at(1);
    freqParam = value.mid(2, 3);
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
				}
				else {  //not region 2
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
    int numSpaces1 = 2;
    int numSpaces2 = 10 - theCall.length();
    int numSpaces3 = 11 - freq.length();
    QString spaces1_theCall = QString(numSpaces1, ' ').append(theCall);
    QString spaces2_theFreq = QString(numSpaces2, ' ').append(freq);
    QString spaces3_theMode = QString(numSpaces3, ' ').append(mode);
    QString qsyMessageString = theTime + spaces1_theCall + spaces2_theFreq + spaces3_theMode;
    ui->QSYMonitorTextBrowser->append(qsyMessageString);
  }
}

