#include "QSYMessageCreator.h"
#include <QApplication>
#include <QSettings>
#include <QRadioButton>
#include <QButtonGroup>
#include <QObject>
#include <QMessageBox>
#include <QInputDialog>
#include <QCloseEvent>
#include <QTimer>
#include "SettingsGroup.hpp"
#include "Configuration.hpp"
#include "ui_QSYMessageCreator.h"


QSYMessageCreator::QSYMessageCreator(QSettings * settings, Configuration const * configuration, QWidget *parent) :
  QWidget {parent},
  settings_ {settings},
  configuration_ {configuration},
  ui {new Ui::QSYMessageCreator}
{
  ui->setupUi(this);
  read_settings ();
  setWindowTitle ("QSYMessageCreator");
  QButtonGroup *modeButtonGroup = new QButtonGroup;
  modeButtonGroup->addButton(ui->radioButFM);
  modeButtonGroup->addButton(ui->radioButSSB);
  modeButtonGroup->addButton(ui->radioButCW);
  modeButtonGroup->addButton(ui->radioButFT8);
  modeButtonGroup->addButton(ui->radioButMSK);

  QButtonGroup *bandButtonGroup = new QButtonGroup;
  bandButtonGroup->addButton(ui->radioBut50);
  bandButtonGroup->addButton(ui->radioBut144);
  bandButtonGroup->addButton(ui->radioBut222);
  bandButtonGroup->addButton(ui->radioBut432);
  bandButtonGroup->addButton(ui->radioBut902);
  bandButtonGroup->addButton(ui->radioBut903);
  bandButtonGroup->addButton(ui->radioBut1296);
  bandButtonGroup->addButton(ui->radioBut2304);
  bandButtonGroup->addButton(ui->radioBut3400);
  bandButtonGroup->addButton(ui->radioBut5760);
  bandButtonGroup->addButton(ui->radioBut10368);
  bandButtonGroup->addButton(ui->radioBut24192);

#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
  QObject::connect(modeButtonGroup, &QButtonGroup::idToggled, [&](int id, bool checked) {
      qDebug() << "Button" << id << "toggled:" << checked;
      QString theBand = getBand();
      QString theMode = getMode(theBand);
      WriteMessage(theBand, theMode);
  });

  QObject::connect(bandButtonGroup, &QButtonGroup::idToggled, [&](int id, bool checked) {
      qDebug() << "Button" << id << "toggled:" << checked;
      QString theBand = getBand();
      QString theMode = getMode(theBand);
      WriteMessage(theBand, theMode);
  });
#endif

}

QSYMessageCreator::~QSYMessageCreator()
{
  delete ui;
}

void QSYMessageCreator::on_button1_clicked()
{
  QString theBand = getBand();
  QString theMode = getMode(theBand);
  QString message = WriteMessage(theBand, theMode);
  Q_EMIT sendMessage(message);
}

void QSYMessageCreator::setQSYMessageCreatorStatusFalse()
{
  Q_EMIT sendQSYMessageCreatorStatus(false);
}

void QSYMessageCreator::closeEvent (QCloseEvent * e)
{
  write_settings ();
  setQSYMessageCreatorStatusFalse();
  e->accept();                 // was ignore
}


void QSYMessageCreator::read_settings ()
{
  SettingsGroup g (settings_, "QSYMessageCreator");
  move (settings_->value ("window/pos", pos ()).toPoint ());
  ui->kHzBox->setValue(settings_->value("kHz").toInt());
  setBand(settings_->value("band").toString());
  setMode(settings_->value("band").toString(), settings_->value("mode").toString());
  WriteMessage(settings_->value("band").toString(), settings_->value("mode").toString());
}

void QSYMessageCreator::write_settings ()
{
  SettingsGroup g (settings_, "QSYMessageCreator");
  settings_->setValue ("window/pos", pos ());
  settings_->setValue ("band", getBand());
  settings_->setValue ("mode", getMode(getBand()));
  settings_->setValue ("kHz", ui->kHzBox->value());
}

QString QSYMessageCreator::WriteMessage (QString band, QString mode)
{
    qint16 kHzFreq = ui->kHzBox->value();
    QString kHzStr = QString::number(kHzFreq);
    QString message = "$DX " + band + mode + kHzStr;
    ui->messageLabel->setText(message);
    return message;
}


QString QSYMessageCreator::getBand()
{
  QString band;
  if (ui->radioBut50->isChecked())
  {
    band = "A";
  }
  else if (ui->radioBut144->isChecked())
  {
    band = "B";
  }
  else if (ui->radioBut222->isChecked())
  {
    band = "C";
  }
  else if (ui->radioBut432->isChecked())
  {
    band = "D";
  }
  else if (ui->radioBut902->isChecked())
  {
    band = "92";
  }
  else if (ui->radioBut903->isChecked())
  {
    band = "93";
  }
  else if (ui->radioBut1296->isChecked())
  {
    band = "E";
  }
  else if (ui->radioBut2304->isChecked())
  {
    band = "F";
  }
  else if (ui->radioBut3400->isChecked())
  {
    band = "G";
  }
  else if (ui->radioBut5760->isChecked())
  {
    band = "H";
  }
  else if (ui->radioBut10368->isChecked())
  {
    band = "I";
  }
  else if (ui->radioBut24192->isChecked())
  {
    band = "J";
  }
  return band;

}


void QSYMessageCreator::setBand(QString band)
{
    if (band == "A")
    {
        ui->radioBut50->setChecked(true);
    }
    else if (band == "B")
    {
        ui->radioBut144->setChecked(true);
    }
    else if (band == "C")
    {
        ui->radioBut222->setChecked(true);
    }
    else if (band == "D")
    {
        ui->radioBut432->setChecked(true);
    }
    else if (band == "92")
    {
        ui->radioBut902->setChecked(true);
    }
    else if (band == "93")
    {
        ui->radioBut903->setChecked(true);
    }
    else if (band == "E")
    {
        ui->radioBut1296->setChecked(true);
    }
    else if (band == "F")
    {
        ui->radioBut2304->setChecked(true);
    }
    else if (band == "G")
    {
        ui->radioBut3400->setChecked(true);
    }
    else if (band == "H")
    {
        ui->radioBut5760->setChecked(true);
    }
    else if (band == "I")
    {
        ui->radioBut10368->setChecked(true);
    }
    else if (band == "J")
    {
        ui->radioBut24192->setChecked(true);
    }

}

void QSYMessageCreator::setMode(QString band, QString mode)
{
    QString MHz;
    if(mode=="V")
    {
        ui->radioButSSB->setChecked(true);
        ui->messageLabel_FM->setHidden(true);
        ui->label_2_FM->setHidden(true);
    }
    else if(mode=="W")
    {
        ui->radioButCW->setChecked(true);
        ui->messageLabel_FM->setHidden(true);
        ui->label_2_FM->setHidden(true);
    }
    else if(mode=="4")
    {
        ui->radioButMSK->setChecked(true);
        ui->messageLabel_FM->setHidden(true);
        ui->label_2_FM->setHidden(true);
    }
    else if(mode=="8")
    {
        ui->radioButFT8->setChecked(true);
        ui->messageLabel_FM->setHidden(true);
        ui->label_2_FM->setHidden(true);
    }
    else if(mode=="M")
    {
        ui->radioButFM->setChecked(true);
        if(band == "A")
        {
            MHz = "52";
        }
        else if (band == "B")
        {
            MHz = "146";
        }
        else if (band == "C")
        {
            MHz = "223";
        }
        else if (band == "D")
        {
            MHz = "446";
        }
        if(band =="A" || band == "B" || band == "C" || band == "D")
        {
            ui->messageLabel_FM->setHidden(false);
            ui->label_2_FM->setHidden(false);
            ui->messageLabel_FM->setText(MHz);
        }
        else
        {
            ui->messageLabel_FM->setHidden(true);
            ui->label_2_FM->setHidden(true);
        }
    }

}

QString QSYMessageCreator::getMode(QString band)
{
  QString mode;
  QString  MHz;
  if (ui->radioButSSB->isChecked())
  {
    mode = "V";
  }
  else if (ui->radioButFM->isChecked())
  {
    mode = "M";
    if(band == "A")
    {
      MHz = "52";
    }
    else if (band == "B")
    {
      MHz = "146";
    }
    else if (band == "C")
    {
      MHz = "223";
    }
    else if (band == "D")
    {
      MHz = "446";
    }
  }
  else if (ui->radioButCW->isChecked())
  {
    mode = "W";
  }
  else if (ui->radioButFT8->isChecked())
  {
    mode = "8";
  }
  else if (ui->radioButMSK->isChecked())
  {
    mode = "4";
  }
  if((band =="A" || band == "B" || band == "C" || band == "D") && (mode =="M"))
  {
    ui->messageLabel_FM->setHidden(false);
    ui->label_2_FM->setHidden(false);
    ui->messageLabel_FM->setText(MHz);
  }
  else
  {
    ui->messageLabel_FM->setHidden(true);
    ui->label_2_FM->setHidden(true);
  }
  return mode;
}
