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
#include <QThread>
#include <QTabWidget>
#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QWidget>
#include <QLayout>
#include <QSpinBox>
#include <QMap>
#include "commons.h"
#include "SettingsGroup.hpp"
#include "Configuration.hpp"
#include "qt_helpers.hpp"
#include "QSYMessageCreator.h"
#include "ui_QSYMessageCreator.h"

QSYMessageCreator::QSYMessageCreator(QSettings * settings, Configuration const * configuration, QWidget *parent)
    : QWidget(parent),
    settings_ {settings},
    configuration_ {configuration},
    ui(new Ui::QSYMessageCreator) {
  ui->setupUi(this);
  setWindowTitle ("MessageCreator");
  QButtonGroup *modeButtonGroup = new QButtonGroup;
  modeButtonGroup-> setExclusive(true);
  modeButtonGroup->addButton(ui->radioButFM);
  modeButtonGroup->addButton(ui->radioButSSB);
  modeButtonGroup->addButton(ui->radioButCW);
  modeButtonGroup->addButton(ui->radioButFT8);
  modeButtonGroup->addButton(ui->radioButMSK);

  QButtonGroup *bandButtonGroup = new QButtonGroup;
  bandButtonGroup-> setExclusive(true);
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
  bandButtonGroup->addButton(ui->radioBut40);
  bandButtonGroup->addButton(ui->radioBut70);

  QButtonGroup *modeButtonGroup2 = new QButtonGroup;
  modeButtonGroup2-> setExclusive(true);
  modeButtonGroup2->addButton(ui->radioButFM2);
  modeButtonGroup2->addButton(ui->radioButSSB2);
  modeButtonGroup2->addButton(ui->radioButCW2);
  modeButtonGroup2->addButton(ui->radioButFT82);
  modeButtonGroup2->addButton(ui->radioButFT4);
  modeButtonGroup2->addButton(ui->radioButJT9);
  modeButtonGroup2->addButton(ui->radioButJT65);
  modeButtonGroup2->addButton(ui->radioButFST4);

  QButtonGroup *bandButtonGroup2 = new QButtonGroup;
  bandButtonGroup2-> setExclusive(true);
  bandButtonGroup2->addButton(ui->radioBut630M);
  bandButtonGroup2->addButton(ui->radioBut160M);
  bandButtonGroup2->addButton(ui->radioBut80M);
  bandButtonGroup2->addButton(ui->radioBut60M);
  bandButtonGroup2->addButton(ui->radioBut40M);
  bandButtonGroup2->addButton(ui->radioBut30M);
  bandButtonGroup2->addButton(ui->radioBut20M);
  bandButtonGroup2->addButton(ui->radioBut17M);
  bandButtonGroup2->addButton(ui->radioBut15M);
  bandButtonGroup2->addButton(ui->radioBut12M);
  bandButtonGroup2->addButton(ui->radioBut10M1);
  bandButtonGroup2->addButton(ui->radioBut10M2);

  QButtonGroup *modeButtonGroup3 = new QButtonGroup;
  modeButtonGroup3-> setExclusive(true);
  modeButtonGroup3->addButton(ui->radioButQ6530B);
  modeButtonGroup3->addButton(ui->radioButQ6560C);
  modeButtonGroup3->addButton(ui->radioButCW3);
  modeButtonGroup3->addButton(ui->radioButQ6560D);
  modeButtonGroup3->addButton(ui->radioButQ6560E);
  modeButtonGroup3->addButton(ui->radioButQ65120D);

  QButtonGroup *bandButtonGroup3 = new QButtonGroup;
  bandButtonGroup3-> setExclusive(true);
  bandButtonGroup3->addButton(ui->radioBut50A);
  bandButtonGroup3->addButton(ui->radioBut144A);
  bandButtonGroup3->addButton(ui->radioBut222A);
  bandButtonGroup3->addButton(ui->radioBut432A);
  bandButtonGroup3->addButton(ui->radioBut902A);
  bandButtonGroup3->addButton(ui->radioBut903A);
  bandButtonGroup3->addButton(ui->radioBut1296A);
  bandButtonGroup3->addButton(ui->radioBut2304A);
  bandButtonGroup3->addButton(ui->radioBut3400A);
  bandButtonGroup3->addButton(ui->radioBut5760A);
  bandButtonGroup3->addButton(ui->radioBut10368A);
  bandButtonGroup3->addButton(ui->radioBut24048);

  QButtonGroup *messageButtonGroup = new QButtonGroup;
  messageButtonGroup-> setExclusive(true);
  messageButtonGroup->addButton(ui->message1);
  messageButtonGroup->addButton(ui->message2);
  messageButtonGroup->addButton(ui->message3);
  messageButtonGroup->addButton(ui->message4);
  messageButtonGroup->addButton(ui->message5);
  messageButtonGroup->addButton(ui->message6);
  messageButtonGroup->addButton(ui->message7);
  messageButtonGroup->addButton(ui->message8);
  messageButtonGroup->addButton(ui->message9);
  messageButtonGroup->addButton(ui->message10);
  messageButtonGroup->addButton(ui->message11);
  messageButtonGroup->addButton(ui->message12);
  messageButtonGroup->addButton(ui->message13);
  messageButtonGroup->addButton(ui->message14);

  QPair<QString,QString> key("L","V"); //630m //SSB
  key.first = "M";  //160
  kHzFreqMap.insert(key, 910);
  key.first = "N"; //80
  kHzFreqMap.insert(key, 960);
  key.first = "O"; //60
  kHzFreqMap.insert(key, 354);
  key.first = "P"; //40
  kHzFreqMap.insert(key, 285);
  key.first = "R"; //20
  kHzFreqMap.insert(key, 285);
  key.first = "S"; //17
  kHzFreqMap.insert(key, 130);
  key.first = "T"; //15
  kHzFreqMap.insert(key, 385);
  key.first = "U"; //12
  kHzFreqMap.insert(key, 910);
  key.first = "V"; //28M
  kHzFreqMap.insert(key, 385);

  key.first = "L"; //630M
  key.second = "W"; //CW
  kHzFreqMap.insert(key, 473);
  key.first = "M";  //160
  kHzFreqMap.insert(key, 810);
  key.first = "N"; //80
  kHzFreqMap.insert(key, 560);
  key.first = "O"; //60
  kHzFreqMap.insert(key, 353);
  key.first = "P"; //40
  kHzFreqMap.insert(key, 40);
  key.first = "Q"; //30
  kHzFreqMap.insert(key, 106);
  key.first = "R"; //20
  kHzFreqMap.insert(key, 60);
  key.first = "S"; //17
  kHzFreqMap.insert(key, 96);
  key.first = "T"; //15
  kHzFreqMap.insert(key, 60);
  key.first = "U"; //12
  kHzFreqMap.insert(key, 906);
  key.first = "V"; //28M
  kHzFreqMap.insert(key, 60);

  key.first = "M"; //29 MHz
  key.second = "M"; //FM
  kHzFreqMap.insert(key, 600);

  key.first = "L"; //630M
  key.second = "C"; //FST4
  kHzFreqMap.insert(key, 474);
  key.first = "M";  //160
  kHzFreqMap.insert(key, 839);
  key.first = "N"; //80
  kHzFreqMap.insert(key, 577);
  key.first = "O"; //60
  kHzFreqMap.insert(key, 357);
  key.first = "P"; //40
  kHzFreqMap.insert(key, 49);
  key.first = "Q"; //30
  kHzFreqMap.insert(key, 142);
  key.first = "R"; //20
  kHzFreqMap.insert(key, 82);
  key.first = "S"; //17
  kHzFreqMap.insert(key, 106);
  key.first = "T"; //15
  kHzFreqMap.insert(key, 142);
  key.first = "U"; //12
  kHzFreqMap.insert(key, 921);
  key.first = "V"; //28M
  kHzFreqMap.insert(key, 182);

  key.second = "2"; //FT4
  key.first = "N"; //80
  kHzFreqMap.insert(key, 575);
  key.first = "O"; //60
  kHzFreqMap.insert(key, 357);
  key.first = "P"; //40
  kHzFreqMap.insert(key, 47);
  key.first = "Q"; //30
  kHzFreqMap.insert(key, 140);
  key.first = "R"; //20
  kHzFreqMap.insert(key, 80);
  key.first = "S"; //17
  kHzFreqMap.insert(key, 104);
  key.first = "T"; //15
  kHzFreqMap.insert(key, 140);
  key.first = "U"; //12
  kHzFreqMap.insert(key, 919);
  key.first = "V"; //28M
  kHzFreqMap.insert(key, 180);

  key.second = "8"; //FT8;
  key.first = "M";  //160
  kHzFreqMap.insert(key, 840);
  key.first = "N"; //80
  kHzFreqMap.insert(key, 573);
  key.first = "O"; //60
  kHzFreqMap.insert(key, 357);
  key.first = "P"; //40
  kHzFreqMap.insert(key, 74);
  key.first = "Q"; //30
  kHzFreqMap.insert(key, 136);
  key.first = "R"; //20
  kHzFreqMap.insert(key, 74);
  key.first = "S"; //17
  kHzFreqMap.insert(key, 100);
  key.first = "T"; //15
  kHzFreqMap.insert(key, 74);
  key.first = "U"; //12
  kHzFreqMap.insert(key, 915);
  key.first = "V"; //28M
  kHzFreqMap.insert(key, 74);

  key.second = "B"; //JT65
  key.first = "M";  //160
  kHzFreqMap.insert(key, 838);
  key.first = "N"; //80
  kHzFreqMap.insert(key, 576);
  key.first = "O"; //60
  kHzFreqMap.insert(key, 357);
  key.first = "P"; //40
  kHzFreqMap.insert(key, 76);
  key.first = "Q"; //30
  kHzFreqMap.insert(key, 138);
  key.first = "R"; //20
  kHzFreqMap.insert(key, 76);
  key.first = "S"; //17
  kHzFreqMap.insert(key, 102);
  key.first = "T"; //15
  kHzFreqMap.insert(key, 76);
  key.first = "U"; //12
  kHzFreqMap.insert(key, 917);
  key.first = "V"; //28M
  kHzFreqMap.insert(key, 76);

  key.second = "A"; //JT9
  key.first = "M";  //160
  kHzFreqMap.insert(key, 839);
  key.first = "N"; //80
  kHzFreqMap.insert(key, 572);
  key.first = "O"; //60
  kHzFreqMap.insert(key, 357);
  key.first = "P"; //40
  kHzFreqMap.insert(key, 78);
  key.first = "Q"; //30
  kHzFreqMap.insert(key, 140);
  key.first = "R"; //20
  kHzFreqMap.insert(key, 78);
  key.first = "S"; //17
  kHzFreqMap.insert(key, 104);
  key.first = "T"; //15
  kHzFreqMap.insert(key, 78);
  key.first = "U"; //12
  kHzFreqMap.insert(key, 919);
  key.first = "V"; //28M
  kHzFreqMap.insert(key, 78);

  //VHF and EME band/mode/freq

  key.second = "V"; //SSB
  key.first = "A";  // 6M
  kHzFreqMap.insert(key, 125);
  key.first = "B"; //2M
  kHzFreqMap.insert(key, 200);
  key.first = "C"; //222
  kHzFreqMap.insert(key, 100);
  key.first = "D"; //432
  kHzFreqMap.insert(key, 100);
  key.first = "9"; //902
  kHzFreqMap.insert(key, 100);
  key.first = "K"; //903
  kHzFreqMap.insert(key, 100);
  key.first = "E"; //1296
  kHzFreqMap.insert(key, 100);
  key.first = "F"; //2304
  kHzFreqMap.insert(key, 100);
  key.first = "G"; //3400
  kHzFreqMap.insert(key, 100);
  key.first = "H";  //5760
  kHzFreqMap.insert(key, 100);
  key.first = "I"; //10368
  kHzFreqMap.insert(key, 100);
  key.first = "J"; //24192
  kHzFreqMap.insert(key, 100);
  key.first = "X"; //24048
  kHzFreqMap.insert(key, 100);
  key.first = "4"; //8M
  kHzFreqMap.insert(key, 680);
  key.first = "7"; //4M
  kHzFreqMap.insert(key, 200);

  key.second = "W"; //CW
  key.first = "A";  // 6M
  kHzFreqMap.insert(key, 90);
  key.first = "B"; //2M
  kHzFreqMap.insert(key, 100);
  key.first = "C"; //222
  kHzFreqMap.insert(key, 100);
  key.first = "D"; //432
  kHzFreqMap.insert(key, 100);
  key.first = "9"; //902
  kHzFreqMap.insert(key, 100);
  key.first = "K"; //903
  kHzFreqMap.insert(key, 100);
  key.first = "E"; //1296
  kHzFreqMap.insert(key, 100);
  key.first = "F"; //2304
  kHzFreqMap.insert(key, 100);
  key.first = "G"; //3400
  kHzFreqMap.insert(key, 100);
  key.first = "H";  //5760
  kHzFreqMap.insert(key, 100);
  key.first = "I"; //10368
  kHzFreqMap.insert(key, 100);
  key.first = "J"; //24192
  kHzFreqMap.insert(key, 100);
  key.first = "X"; //24048
  kHzFreqMap.insert(key, 100);

  key.second = "M"; // FM
  key.first = "A";  // 6M
  kHzFreqMap.insert(key, 525);
  key.first = "B"; //2M
  kHzFreqMap.insert(key, 520);
  key.first = "C"; //222
  kHzFreqMap.insert(key, 500);
  key.first = "D"; //432
  kHzFreqMap.insert(key, 000);

  key.second = "8"; // FT8
  key.first = "A";  // 6M
  kHzFreqMap.insert(key, 313);
  key.first = "B"; //2M
  kHzFreqMap.insert(key, 174);
  key.first = "C"; //222
  kHzFreqMap.insert(key, 174);
  key.first = "D"; //432
  kHzFreqMap.insert(key, 174);
  key.first = "9"; //902
  kHzFreqMap.insert(key, 174);
  key.first = "K"; //903
  kHzFreqMap.insert(key, 174);
  key.first = "E"; //1296
  kHzFreqMap.insert(key, 174);
  key.first = "F"; //2304
  kHzFreqMap.insert(key, 174);
  key.first = "G"; //3400
  kHzFreqMap.insert(key, 174);
  key.first = "H";  //5760
  kHzFreqMap.insert(key, 174);
  key.first = "I"; //10368
  kHzFreqMap.insert(key, 174);
  key.first = "J"; //24192
  kHzFreqMap.insert(key, 174);
  key.first = "X"; //24048
  kHzFreqMap.insert(key, 174);
  key.first = "4"; //8M
  kHzFreqMap.insert(key, 680);
  key.first = "7"; //4M
  kHzFreqMap.insert(key, 154);

  key.second = "4"; // MSK144
  key.first = "A";  // 6M
  kHzFreqMap.insert(key, 280);
  key.first = "B"; //2M
  kHzFreqMap.insert(key, 150);
  key.first = "C"; //222
  kHzFreqMap.insert(key, 150);
  key.first = "D"; //432
  kHzFreqMap.insert(key, 150);

  QList<QString> Q65modes = {"D","E","F","G","H"};
  QList<QString>::iterator it = Q65modes.begin();
  while (it!=Q65modes.end()) {
    key.second = *it; // Q65 submodes D,E,F,G,H
    key.first = "A";  // 6M
    kHzFreqMap.insert(key, 275);
    key.first = "B"; //2M
    kHzFreqMap.insert(key, 170);
    key.first = "C"; //222
    kHzFreqMap.insert(key, 170);
    key.first = "D"; //432
    kHzFreqMap.insert(key, 170);
    key.first = "9"; //902
    kHzFreqMap.insert(key, 170);
    key.first = "K"; //903
    kHzFreqMap.insert(key, 170);
    key.first = "E"; //1296
    kHzFreqMap.insert(key, 170);
    key.first = "F"; //2304
    kHzFreqMap.insert(key, 170);
    key.first = "G"; //3400
    kHzFreqMap.insert(key, 170);
    key.first = "H";  //5760
    kHzFreqMap.insert(key, 170);
    key.first = "I"; //10368
    kHzFreqMap.insert(key, 170);
    key.first = "J"; //24192
    kHzFreqMap.insert(key, 170);
    key.first = "X"; //24048
    kHzFreqMap.insert(key, 170);
    ++it;
  }

  read_settings();

#if (QT_VERSION < QT_VERSION_CHECK(5, 15, 0))
  //VHF
  connect(modeButtonGroup, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked){
    qDebug() << "id" << id  << "toggled:" << checked;
    QString theBand = getBand();
    QString theMode = getMode(theBand, configuration_->region());
    WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  connect(bandButtonGroup, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked){
    qDebug() << "id" << id  << "toggled:" << checked;
    QString theBand = getBand();
    QString theMode = getMode(theBand, configuration_->region());
    WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });
  //HF
  connect(modeButtonGroup2, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked){
    qDebug() << "id" << id  << "toggled:" << checked;
    QString theBand = getBand();
    QString theMode = getMode(theBand, configuration_->region());
    WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  connect(bandButtonGroup2, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked){
    qDebug() << "id" << id  << "toggled:" << checked;
    QString theBand = getBand();
    QString theMode = getMode(theBand, configuration_->region());
    WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });
  //EME
  connect(modeButtonGroup3, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked){
    qDebug() << "id" << id  << "toggled:" << checked;
    QString theBand = getBand();
    QString theMode = getMode(theBand, configuration_->region());
    WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  connect(bandButtonGroup3, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked){
    qDebug() << "id" << id  << "toggled:" << checked;
    QString theBand = getBand();
    QString theMode = getMode(theBand, configuration_->region());
    WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  //General Messages
  connect(messageButtonGroup, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked){
    qDebug() << "id" << id  << "toggled:" << checked;
    QString message = getGenMessage();
  });

#else //VHF
  QObject::connect(modeButtonGroup, &QButtonGroup::idToggled, [&](int id, bool checked) {
    qDebug() << "Button" << id << "toggled:" << checked;
    QString theBand = getBand();
    QString theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    QSYMessageCreator::WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  QObject::connect(bandButtonGroup, &QButtonGroup::idToggled, [&](int id, bool checked) {
    qDebug() << "Button" << id << "toggled:" << checked;
    QString theBand = getBand();
    QString theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    QSYMessageCreator::WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });
  //HF
  QObject::connect(modeButtonGroup2, &QButtonGroup::idToggled, [&](int id, bool checked) {
    qDebug() << "Button" << id << "toggled:" << checked;
    QString theBand = getBand();
    QString theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    QSYMessageCreator::WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  QObject::connect(bandButtonGroup2, &QButtonGroup::idToggled, [&](int id, bool checked) {
    qDebug() << "Button" << id << "toggled:" << checked;
    QString theBand = getBand();
    QString theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    QSYMessageCreator::WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });
  //EME
  QObject::connect(modeButtonGroup3, &QButtonGroup::idToggled, [&](int id, bool checked) {
    qDebug() << "Button" << id << "toggled:" << checked;
    QString theBand = getBand();
    QString theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    QSYMessageCreator::WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  QObject::connect(bandButtonGroup3, &QButtonGroup::idToggled, [&](int id, bool checked) {
    qDebug() << "Button" << id << "toggled:" << checked;
    QString theBand = getBand();
    QString theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    QSYMessageCreator::WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  //General Messages
  QObject::connect(messageButtonGroup, &QButtonGroup::idToggled, [&](int id, bool checked) {
    qDebug() << "Button" << id << "toggled:" << checked;
    QString message = getGenMessage();
  });

#endif

  connect(ui->kHzBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &QSYMessageCreator::onkHzBoxValueChanged); //VHF
  connect(ui->kHzBox2, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &QSYMessageCreator::onkHzBox2ValueChanged);  //HF
  connect(ui->kHzBox3, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &QSYMessageCreator::onkHzBox3ValueChanged);  //EME

  connect(ui->tabWidget, &QTabWidget::currentChanged, this, &QSYMessageCreator::onTabChanged);

  setup();
}

QSYMessageCreator::~QSYMessageCreator()
{
  delete ui;
}

void QSYMessageCreator::setkHzBox(QString theBand, QString theMode, int tabNum)
{
  QPair<QString,QString> key(theBand,theMode);
  auto it =  kHzFreqMap.find(key);
  int defaultValue = -1;
  int value =  (it != kHzFreqMap.constEnd()) ? it.value() : defaultValue;

  if (value >= 0) {
    if(tabNum ==1) {
      ui->kHzBox->setValue(value);
      setkHzVHF(value);
    } else if (tabNum ==0) {
      ui->kHzBox2->setValue(value);
      setkHzHF(value);
    } else if (tabNum ==2) {
      ui->kHzBox3->setValue(value);
      setkHzEME(value);
    }
    kHzFreqMap.insert(key, value);
  } else {
    QMap<QPair<QString, QString>, int>::const_iterator it = kHzFreqMap.begin();
    bool running = true;
    while (running && (it !=kHzFreqMap.end())) {
      QPair<QString,QString> key = it.key();
      if (key.first.contains(theBand)) {
        int value = it.value();
        if (tabNum ==1) {
          ui->kHzBox->setValue(value);
          setkHzVHF(value);
        } else if (tabNum ==0) {
          ui->kHzBox2->setValue(value);
          setkHzHF(value);
        } else if (tabNum ==2) {
          ui->kHzBox3->setValue(value);
          setkHzEME(value);
        };
        running = false;
      }
      ++it;
    }
  }

  if (value<0 && tabNum == 0) {
    if (theBand.compare("L")==0) {
      if ((ui->kHzBox2->value() < 472) || ui->kHzBox2->value() > 479) ui->kHzBox2->setValue(472);
    }
    else if (theBand.compare("M")==0) {
      if (ui->kHzBox2->value() < 800 ) ui->kHzBox2->setValue(800);
    }
    else if (theBand.compare("N")==0) {
      if (ui->kHzBox2->value() < 500 ) ui->kHzBox2->setValue(500);
    }
    else if (theBand.compare("O")==0) {
      if (ui->kHzBox2->value() < 330 || ui->kHzBox2->value() > 405) ui->kHzBox2->setValue(357);
    }
    else if (theBand.compare("P")==0) {
      if (ui->kHzBox2->value() > 300) ui->kHzBox2->setValue(0);
    }
    else if (theBand.compare("Q")==0) {
      if (ui->kHzBox2->value() < 100 || ui->kHzBox2->value() > 150) ui->kHzBox2->setValue(100);
    }
    else if (theBand.compare("R")==0) {
      if (ui->kHzBox2->value() > 350) ui->kHzBox2->setValue(0);
    }
    else if (theBand.compare("S")==0) {
      if (ui->kHzBox2->value() < 68 || ui->kHzBox2->value() > 110) ui->kHzBox2->setValue(68);
    }
    else if (theBand.compare("T")==0) {
      if (ui->kHzBox2->value() > 450) ui->kHzBox2->setValue(0);
    }
    else if (theBand.compare("U")==0) {
      if (ui->kHzBox2->value() < 890 || ui->kHzBox2->value() > 930) ui->kHzBox2->setValue(890);
    }
    else if (theBand.compare("W")==0) {
      if (ui->kHzBox2->value() > 700) ui->kHzBox2->setValue(0);
    }
    else if (theBand.compare("4")==0) {
        if (ui->kHzBox2->value() > 680) ui->kHzBox2->setValue(0);
    }
    else if (theBand.compare("7")==0) {
        if (ui->kHzBox2->value() > 200) ui->kHzBox2->setValue(0);
    }
  }
  setkHzHF(ui->kHzBox2->value());
}

void QSYMessageCreator::onkHzBoxValueChanged()
{
  QString theBand = getBand();
  QString theMode = getMode(theBand, configuration_->region());
  QString message = WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  setkHzVHF(ui->kHzBox->value());
  WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
}

void QSYMessageCreator::onkHzBox2ValueChanged()
{
  QString theBand = getBand();
  QString theMode = getMode(theBand, configuration_->region());
  QString message = WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  setkHzHF(ui->kHzBox2->value());
  WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
}

void QSYMessageCreator::onkHzBox3ValueChanged()
{
  QString theBand = getBand();
  QString theMode = getMode(theBand, configuration_->region());
  QString message = WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  setkHzEME(ui->kHzBox3->value());
  WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
}

void QSYMessageCreator::setup()
{
  if (configuration_->region() == 0) {
    QMessageBox *regionWarning = new QMessageBox(this);
    regionWarning->setModal(false);
    regionWarning->setIcon(QMessageBox::Warning);
    regionWarning->setText("You need to enter your IARU Region\nin Settings or FM frequencies\nwill not be shown!");
    regionWarning->setWindowFlags(regionWarning->windowFlags() | Qt::WindowStaysOnTopHint);
    regionWarning->show();
  } else if (configuration_->region() == 2) {
      ui->radioBut24192->setText("24192 MHz");
  } else {
      ui->radioBut24192->setText("24048 MHz");
  }
}

void QSYMessageCreator::onTabChanged()
{
  if (configuration_->region() == 2) {
    ui->radioBut24192->setText("24192 MHz");
  } else {
    ui->radioBut24192->setText("24048 MHz");
  }
  getGenMessage();
  QString theBand = getBand();
  QString theMode = getMode(theBand, configuration_->region());
  setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
  WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
}

void QSYMessageCreator::on_button1_clicked()
{
  QString theBand = getBand();
  QString theMode = getMode(theBand, configuration_->region());
  QString message = WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  QPair<QString,QString>key(theBand,theMode);
  kHzFreqMap.insert(key, ui->kHzBox->value());
  Q_EMIT sendMessage(message);
}

void QSYMessageCreator::on_button2_clicked()
{
  QString theBand = getBand();
  QString theMode = getMode(theBand, configuration_->region());
  QString message = WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  QPair<QString,QString>key(theBand,theMode);
  kHzFreqMap.insert(key, ui->kHzBox2->value());
  Q_EMIT sendMessage(message);
}

void QSYMessageCreator::on_button3_clicked()
{
  QString theBand = getBand();
  QString theMode = getMode(theBand, configuration_->region());
  QString message = WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  QPair<QString,QString>key(theBand,theMode);
  kHzFreqMap.insert(key, ui->kHzBox3->value());
  Q_EMIT sendMessage(message);
}

void QSYMessageCreator::on_genButton_clicked()
{
  QString message = getGenMessage();
  Q_EMIT sendMessage(message);
}

QString QSYMessageCreator::getGenMessage()  {
    QString message;
    if (ui->message1->isChecked()) message = "001";
    else if (ui->message2->isChecked()) message = "002";
    else if (ui->message3->isChecked()) message = "003";
    else if (ui->message4->isChecked()) message = "004";
    else if (ui->message5->isChecked()) message = "005";
    else if (ui->message6->isChecked()) message = "006";
    else if (ui->message7->isChecked()) message = "007";
    else if (ui->message8->isChecked()) message = "008";
    else if (ui->message9->isChecked()) message = "009";
    else if (ui->message10->isChecked()) message = "010";
    else if (ui->message11->isChecked()) message = "011";
    else if (ui->message12->isChecked()) message = "012";
    else if (ui->message13->isChecked()) message = "013";
    else if (ui->message14->isChecked()) message = "014";
    message = ui->DxBaseLabel->text() + " ZA" + message;
    ui->messageLabel4->setText(message);
    return message;
}

void QSYMessageCreator::setQSYMessageCreatorStatusFalse()
{
  Q_EMIT sendQSYMessageCreatorStatus(false);
}

void QSYMessageCreator::getDxBase(QString value)  {
    ui->DxBaseLabel->setText(value);
    QString theBand = getBand();
    QString theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
    getGenMessage();
}

void QSYMessageCreator::closeEvent (QCloseEvent * e)
{
  if ((configuration_->region()) ==2 ) {
    ui->radioBut24192->setText("24192 MHz");
  } else {
    ui->radioBut24192->setText("24048 MHz");
  }
  write_settings();
  setQSYMessageCreatorStatusFalse();
  e->accept();                 // was ignore
}

void QSYMessageCreator::read_settings ()
{
  SettingsGroup g (settings_, "QSYMessageCreator");
  move (settings_->value ("window/pos", pos ()).toPoint ());
  ui->kHzBox3->setValue(settings_->value("kHzEME").toInt());
  setbandEME(settings_->value("bandEME").toString());
  setmodeEME(settings_->value("modeEME").toString());
  ui->kHzBox->setValue(settings_->value("kHzVHF").toInt());
  setbandVHF(settings_->value("bandVHF").toString());
  setmodeVHF(settings_->value("modeVHF").toString());
  ui->kHzBox2->setValue(settings_->value("kHzHF").toInt());
  setbandHF(settings_->value("bandHF").toString());
  setmodeHF(settings_->value("modeHF").toString());
  ui->tabWidget->setCurrentIndex(settings_->value("whichTab").toInt());
  setkHzVHF(ui->kHzBox->value());
  setkHzEME(ui->kHzBox3->value());
  setkHzHF(ui->kHzBox2->value());
  bool testVHF = true;
  bool testHF = true;
  bool testEME = true;
  if ((settings_->value("bandVHF").toString()).compare("")==0) {
    setbandVHF("A");
    testVHF=false;
  }
  if ((settings_->value("modeVHF").toString()).compare("")==0) {
    setmodeVHF("V");
    testVHF=false;
  }
  if ((settings_->value("bandHF").toString()).compare("")==0) {
    setbandHF("M");
    testHF = false;
  }
  if ((settings_->value("modeHF").toString()).compare("")==0) {
    setmodeHF("V");
    testHF = false;
  }
  if ((settings_->value("bandEME").toString()).compare("")==0) {
    setbandEME("A");
    testEME = false;
  }
  if ((settings_->value("modeEME").toString()).compare("")==0) {
    setmodeEME("V");
    testEME = false;
  }
  if ((settings_->value("kHzVHF").toString()).compare("")==0) {
    setkHzVHF(0);
    testVHF = false;
  }
  if ((settings_->value("kHzHF").toString()).compare("")==0) {
    setkHzHF(0);
    testHF = false;
  }
  if ((settings_->value("kHzEME").toString()).compare("")==0) {
    setkHzEME(0);
    testEME = false;
  }

  if (testVHF) {
    QPair<QString,QString> key(getbandVHF(),getmodeVHF());
    kHzFreqMap.insert(key,getkHzVHF());
    }

  if (testEME) {
    QPair<QString,QString> key(getbandEME(),getmodeEME());
    kHzFreqMap.insert(key,getkHzEME());
  }

  if (testHF) {
    QPair<QString,QString> key(getbandHF(),getmodeHF());
    kHzFreqMap.insert(key,getkHzHF());
  }

  ui->tabWidget->setCurrentIndex(1);
  setBand(getbandVHF());
  setMode(getbandVHF(), getmodeVHF(), configuration_->region());
  if (!testVHF) {
    setkHzBox(getbandVHF(),getmodeVHF(),1);
  }
  setkHzVHF(ui->kHzBox->value());;
  WriteMessage(ui->DxBaseLabel->text(), getbandVHF(), getmodeVHF());

  ui->tabWidget->setCurrentIndex(2);
  setBand(getbandEME());
  setMode(getbandEME(), getmodeEME(), configuration_->region());
  if (!testEME) {
    setkHzBox(getbandEME(),getmodeEME(),2);
  }
  setkHzEME(ui->kHzBox3->value());;
  WriteMessage(ui->DxBaseLabel->text(), getbandEME(), getmodeEME());

  ui->tabWidget->setCurrentIndex(0);
  setBand(getbandHF());
  setMode(getbandHF(), getmodeHF(), configuration_->region());
  if (!testHF) {
    setkHzBox(getbandHF(),getmodeHF(),0);
  }
  setkHzHF(ui->kHzBox2->value());
  WriteMessage(ui->DxBaseLabel->text(), getbandHF(), getmodeHF());

  ui->tabWidget->setCurrentIndex(settings_->value("whichTab").toInt());
}

QString QSYMessageCreator::WriteMessage (QString hisCall, QString band, QString mode)
{
  QString message = "";
  if(ui->tabWidget->currentIndex() == 1) {
    qint16 kHzFreq = ui->kHzBox->value();
    QString kHzStr = QStringLiteral("%1").arg(kHzFreq,3,10,QLatin1Char('0'));
    message = hisCall + " " + band + mode + kHzStr;
    ui->messageLabel->setText(message);
  }
  else if(ui->tabWidget->currentIndex() == 0) {
    qint16 kHzFreq = ui->kHzBox2->value();
    QString kHzStr = QStringLiteral("%1").arg(kHzFreq,3,10,QLatin1Char('0'));
    message = hisCall + " " + band + mode + kHzStr;
    ui->messageLabel2->setText(message);
  }
  else if(ui->tabWidget->currentIndex() == 2) {
    qint16 kHzFreq = ui->kHzBox3->value();
    QString kHzStr = QStringLiteral("%1").arg(kHzFreq,3,10,QLatin1Char('0'));
        message = hisCall + " " + band + mode + kHzStr;
    ui->messageLabel3->setText(message);
  }
  return message;
}

QString QSYMessageCreator::getBand()
{
  QString band;
  if(ui->tabWidget->currentIndex() == 1) {
    if (ui->radioBut50->isChecked()) band = "A";
    else if (ui->radioBut144->isChecked()) band = "B";
    else if (ui->radioBut222->isChecked()) band = "C";
    else if (ui->radioBut432->isChecked()) band = "D";
    else if (ui->radioBut902->isChecked()) band = "9";
    else if (ui->radioBut903->isChecked()) band = "K";
    else if (ui->radioBut1296->isChecked()) band = "E";
    else if (ui->radioBut2304->isChecked()) band = "F";
    else if (ui->radioBut3400->isChecked()) band = "G";
    else if (ui->radioBut5760->isChecked()) band = "H";
    else if (ui->radioBut10368->isChecked()) band = "I";
    else if (ui->radioBut24192->isChecked()) band = "J";
    else if (ui->radioBut40->isChecked()) band = "4";
    else if (ui->radioBut70->isChecked()) band = "7";
    setbandVHF(band);
  } else if(ui->tabWidget->currentIndex() == 0) {
    if (ui->radioBut630M->isChecked()) band = "L";
    else if (ui->radioBut160M->isChecked()) band = "M";
    else if (ui->radioBut80M->isChecked()) band = "N";
    else if (ui->radioBut60M->isChecked()) band = "O";
    else if (ui->radioBut40M->isChecked()) band = "P";
    else if (ui->radioBut30M->isChecked()) band = "Q";
    else if (ui->radioBut20M->isChecked()) band = "R";
    else if (ui->radioBut17M->isChecked()) band = "S";
    else if (ui->radioBut15M->isChecked()) band = "T";
    else if (ui->radioBut12M->isChecked()) band = "U";
    else if (ui->radioBut10M1->isChecked()) band = "V";
    else if (ui->radioBut10M2->isChecked()) band = "W";
    setbandHF(band);
  } else if(ui->tabWidget->currentIndex() == 2) {
    if (ui->radioBut50A->isChecked()) band = "A";
    else if (ui->radioBut144A->isChecked()) band = "B";
    else if (ui->radioBut222A->isChecked()) band = "C";
    else if (ui->radioBut432A->isChecked()) band = "D";
    else if (ui->radioBut902A->isChecked()) band = "9";
    else if (ui->radioBut903A->isChecked()) band = "K";
    else if (ui->radioBut1296A->isChecked()) band = "E";
    else if (ui->radioBut2304A->isChecked()) band = "F";
    else if (ui->radioBut3400A->isChecked()) band = "G";
    else if (ui->radioBut5760A->isChecked()) band = "H";
    else if (ui->radioBut10368A->isChecked()) band = "I";
    else if (ui->radioBut24048->isChecked()) band = "X";
    setbandEME(band);
  }
  return band;
}

void QSYMessageCreator::setBand(QString band)
{
  if(ui->tabWidget->currentIndex() == 1) {
    if (band == "A") ui->radioBut50->setChecked(true);
    else if (band == "B") ui->radioBut144->setChecked(true);
    else if (band == "C") ui->radioBut222->setChecked(true);
    else if (band == "D") ui->radioBut432->setChecked(true);
    else if (band == "9") ui->radioBut902->setChecked(true);
    else if (band == "K") ui->radioBut903->setChecked(true);
    else if (band == "E") ui->radioBut1296->setChecked(true);
    else if (band == "F") ui->radioBut2304->setChecked(true);
    else if (band == "G") ui->radioBut3400->setChecked(true);
    else if (band == "H") ui->radioBut5760->setChecked(true);
    else if (band == "I") ui->radioBut10368->setChecked(true);
    else if (band == "J") ui->radioBut24192->setChecked(true);
    else if (band == "4") ui->radioBut40->setChecked(true);
    else if (band == "7") ui->radioBut70->setChecked(true);
  } else if(ui->tabWidget->currentIndex() == 0) {
    if (band == "L") ui->radioBut630M->setChecked(true);
    else if (band == "M") ui->radioBut160M->setChecked(true);
    else if (band == "N") ui->radioBut80M->setChecked(true);
    else if (band == "O") ui->radioBut60M->setChecked(true);
    else if (band == "P") ui->radioBut40M->setChecked(true);
    else if (band == "Q") ui->radioBut30M->setChecked(true);
    else if (band == "R") ui->radioBut20M->setChecked(true);
    else if (band == "S") ui->radioBut17M->setChecked(true);
    else if (band == "T") ui->radioBut15M->setChecked(true);
    else if (band == "U") ui->radioBut12M->setChecked(true);
    else if (band == "V") ui->radioBut10M1->setChecked(true);
    else if (band == "W") ui->radioBut10M2->setChecked(true);
  } else if(ui->tabWidget->currentIndex() == 2) {
    if (band == "A") ui->radioBut50A->setChecked(true);
    else if (band == "B") ui->radioBut144A->setChecked(true);
    else if (band == "C") ui->radioBut222A->setChecked(true);
    else if (band == "D") ui->radioBut432A->setChecked(true);
    else if (band == "9") ui->radioBut902A->setChecked(true);
    else if (band == "K") ui->radioBut903A->setChecked(true);
    else if (band == "E") ui->radioBut1296A->setChecked(true);
    else if (band == "F") ui->radioBut2304A->setChecked(true);
    else if (band == "G") ui->radioBut3400A->setChecked(true);
    else if (band == "H") ui->radioBut5760A->setChecked(true);
    else if (band == "I") ui->radioBut10368A->setChecked(true);
    else if (band == "X") ui->radioBut24048->setChecked(true);
  }
}

void QSYMessageCreator::setMode(QString band, QString mode, int region)
{
  QString MHz;
  if(ui->tabWidget->currentIndex() == 1) {
    if (mode=="V") {
      ui->radioButSSB->setChecked(true);
      ui->messageLabel_FM->setHidden(true);
      ui->label_2_FM->setHidden(true);
    }
    else if (mode=="W") {
      ui->radioButCW->setChecked(true);
      ui->messageLabel_FM->setHidden(true);
      ui->label_2_FM->setHidden(true);
    }
    else if (mode=="4") {
      ui->radioButMSK->setChecked(true);
      ui->messageLabel_FM->setHidden(true);
      ui->label_2_FM->setHidden(true);
    }
    else if (mode=="8") {
      ui->radioButFT8->setChecked(true);
      ui->messageLabel_FM->setHidden(true);
      ui->label_2_FM->setHidden(true);
    }
    else if (mode=="M") {
      ui->radioButFM->setChecked(true);
      if (band == "A") {
        if (region==2) {
          MHz = "52";
        } else {
          MHz = "51";
        }
      }
      else if (band == "B") {
        if (region==2) {
          MHz = "146";
        } else {
          MHz = "145";
        }
      }
      else if (band == "C") {
        if (region==2) {
          MHz = "223";
        } else {
          MHz = "";
        }
      }
      else if (band == "D") {
        if (region==2) {
          MHz = "446";
        } else {
          MHz = "433";
        }
      }
      if ((band =="A" || band == "B" || band == "C" || band == "D") && (configuration_->region () > 0)) {
        ui->label_2_FM->setText("Region " + QString::number(configuration_->region()) + " MHz");
        ui->messageLabel_FM->setHidden(false);
        ui->label_2_FM->setHidden(false);
        ui->messageLabel_FM->setText(MHz);
        if (region !=2 &&(band=="C")) {
          ui->messageLabel_FM->setHidden(true);
          ui->label_2_FM->setHidden(true);
        }
      } else {
        ui->messageLabel_FM->setHidden(true);
        ui->label_2_FM->setHidden(true);
      }
    }
  } else if(ui->tabWidget->currentIndex() == 0) {
    if (mode=="V") {
      ui->radioButSSB2->setChecked(true);
    }
    else if (mode=="W") {
      ui->radioButCW2->setChecked(true);
    }
    else if (mode=="M") {
      ui->radioButFM2->setChecked(true);
    }
    else if (mode=="8") {
      ui->radioButFT82->setChecked(true);
    }
    else if (mode=="2") {
      ui->radioButFT4->setChecked(true);
    }
    else if (mode=="4") {
        ui->radioButMSK->setChecked(true);
    }
    else if (mode=="A") {
      ui->radioButJT9->setChecked(true);
    }
    else if (mode=="B") {
      ui->radioButJT65->setChecked(true);
    }
    else if (mode=="C") {
      ui->radioButFST4->setChecked(true);
    }
  } else if(ui->tabWidget->currentIndex() == 2) {
    if (mode=="D") {
      ui->radioButQ6530B->setChecked(true);
    }
    else if (mode=="W") {
      ui->radioButCW3->setChecked(true);
    }
    else if (mode=="E") {
      ui->radioButQ6560C->setChecked(true);
    }
    else if (mode=="F") {
      ui->radioButQ6560D->setChecked(true);
    }
    else if (mode=="G") {
      ui->radioButQ6560E->setChecked(true);
    }
    else if (mode=="H") {
      ui->radioButQ65120D->setChecked(true);
    }
  }
}

QString QSYMessageCreator::getMode(QString band,int region)
{
  QString mode;
  QString  MHz;
  qDebug() << "band" << band;
  qDebug() << "band" << region;

  if(ui->tabWidget->currentIndex() == 1) {
    if (ui->radioButSSB->isChecked()) {
      mode = "V";
    }  else if (ui->radioButFM->isChecked()) {
      mode = "M";
      if(band == "A") {
        if (region==2) {
          MHz = "52";
        } else {
          MHz = "51";
        }
      }
      else if (band == "B") {
        if(region==2) {
          MHz = "146";
        } else {
          MHz = "145";
        }
      }
      else if (band == "C") {
        if (region==2) {
          MHz = "223";
        } else {
          MHz = "";
        }
      }
      else if (band == "D") {
        if (region==2) {
          MHz = "446";
        } else {
          MHz = "433";
        }
      }
    }
    else if (ui->radioButCW->isChecked()) {
      mode = "W";
    }
    else if (ui->radioButFT8->isChecked()) {
      mode = "8";
    }
    else if (ui->radioButMSK->isChecked()) {
      mode = "4";
    } else mode = "V";
    if ((band =="A" || band == "B" || band == "C" || band == "D") && (mode =="M") && (configuration_->region () > 0)) {
      ui->label_2_FM->setText("Region " + QString::number(configuration_->region()) + " MHz");
      ui->messageLabel_FM->setHidden(false);
      ui->label_2_FM->setHidden(false);
      ui->messageLabel_FM->setText(MHz);
      if(region !=2 &&(band=="C"))  {
        ui->messageLabel_FM->setHidden(true);
        ui->label_2_FM->setHidden(true);
      }
    } else {
      ui->messageLabel_FM->setHidden(true);
      ui->label_2_FM->setHidden(true);
    }
    setmodeVHF(mode);
  } else if(ui->tabWidget->currentIndex() == 0) {
    if (ui->radioButSSB2->isChecked()) {
      mode = "V";
    }
    else if (ui->radioButFM2->isChecked()) {
      mode = "M";
    }
    else if (ui->radioButCW2->isChecked()) {
      mode = "W";
    }
    else if (ui->radioButFT82->isChecked()) {
      mode = "8";
    }
    else if (ui->radioButFT4->isChecked()) {
      mode = "2";
    }
    else if (ui->radioButJT9->isChecked()) {
      mode = "A";
    }
    else if (ui->radioButJT65->isChecked()) {
      mode = "B";
    }
    else if (ui->radioButFST4->isChecked()) {
      mode = "C";
    } else mode = "V";
    setmodeHF(mode);
  } else if(ui->tabWidget->currentIndex() == 2) {
    if (ui->radioButQ6530B->isChecked()) {
      mode = "D";
    }
    else if (ui->radioButQ6560C->isChecked()) {
      mode = "E";
    }
    else if (ui->radioButCW3->isChecked()) {
      mode = "W";
    }
    else if (ui->radioButQ6560D->isChecked()) {
      mode = "F";
    }
    else if (ui->radioButQ6560E->isChecked()) {
      mode = "G";
    }
    else if (ui->radioButQ65120D->isChecked()) {
      mode = "H";
    } else mode = "V";
    setmodeEME(mode);
  }
  return mode;
}

void QSYMessageCreator::send_message(const QString message) {
  Q_EMIT sendMessage(message);
}

void QSYMessageCreator::write_settings ()
{
  SettingsGroup g (settings_, "QSYMessageCreator");
  settings_->setValue ("window/pos", pos ());
  settings_->setValue ("whichTab", ui->tabWidget->currentIndex());
  settings_->setValue ("bandVHF", getbandVHF());
  settings_->setValue ("modeVHF", getmodeVHF());
  settings_->setValue ("kHzVHF", getkHzVHF());
  settings_->setValue ("bandHF", getbandHF());
  settings_->setValue ("modeHF", getmodeHF());
  settings_->setValue ("kHzHF", getkHzHF());
  settings_->setValue ("bandEME", getbandEME());
  settings_->setValue ("modeEME", getmodeEME());
  settings_->setValue ("kHzEME", getkHzEME());
}

