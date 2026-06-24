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
#include <QProxyStyle>
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
  setWindowTitle (QApplication::applicationName () + " - " + tr ("MessageCreator"));
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

  read_settings();

  if(MHzFreqMap.size() < 4) {
    // need a MHZ map now that have selectable MHz values for FM mode
    if(configuration_->region() > 0) {
      if(configuration_->region() ==2) {
        MHzFreqMap.insert("A",52); //6M, 52 MHz
        MHzFreqMap.insert("B",146); //2M, 146 MHz
        MHzFreqMap.insert("C",223); //1.25M, 223 MHz
        MHzFreqMap.insert("D",446); //70cm, 446 MHz
        MHzFreqMap.insert("E",1294); //23 cm, 1294 MHz
      } else {
        MHzFreqMap.insert("A",50); //6M 50 MHz
        MHzFreqMap.insert("B",145); //2M, 145 MHz
        MHzFreqMap.insert("D",433); //70cm, 433 MHz
        MHzFreqMap.insert("E",1297); //23 cm, 1297 MHz
      }
    }
  }

  if(kHzFreqMap.size() < 10) {
    QPair<QString,QChar> key("L",'V'); //630m //SSB
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
    key.second = 'W'; //CW
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

    key.first = "V"; //28 MHz
    key.second = '9'; //FM
    kHzFreqMap.insert(key, 999);
    key.first = "W"; //29 MHz
    key.second = '9'; //FM
    kHzFreqMap.insert(key, 600);

    key.first = "L"; //630M
    key.second = 'C'; //FST4
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

    key.second = 'J'; //FT4
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

    key.second = 'L'; //FT8;
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

    key.second = 'B'; //JT65
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

    key.second = 'A'; //JT9
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

    key.second = 'V'; //SSB
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

    key.second = 'W'; //CW
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

    key.second = '2'; // FM
    key.first = "A";  // 6M
    kHzFreqMap.insert(key, 525); //region 2
    key.second = '6'; // FM
    key.first = "B"; //2M
    kHzFreqMap.insert(key, 520); //region 2
    key.second = '3'; // FM
    key.first = "C"; //222
    kHzFreqMap.insert(key, 500); //region 2
    key.second = '6'; // FM
    key.first = "D"; //432
    kHzFreqMap.insert(key, 000); //region 2
    key.second = '7'; // FM
    key.first = "E"; //1296
    kHzFreqMap.insert(key, 500); //region 1
    key.second = '4'; // FM
    key.first = "E"; //1296
    kHzFreqMap.insert(key, 500); //region 2
    key.second = '1'; // FM
    key.first = "A";  // 6M
    kHzFreqMap.insert(key, 800); //region 1
    key.second = '5'; // FM
    key.first = "B"; //2M
    kHzFreqMap.insert(key, 500); //region 1
    key.second = '3'; // FM
    key.first = "D"; //432
    kHzFreqMap.insert(key, 650); //region 1

    key.second = 'L'; // FT8
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

    key.second = 'K'; // MSK144
    key.first = "A";  // 6M
    kHzFreqMap.insert(key, 280);
    key.first = "B"; //2M
    kHzFreqMap.insert(key, 150);
    key.first = "C"; //222
    kHzFreqMap.insert(key, 150);
    key.first = "D"; //432
    kHzFreqMap.insert(key, 150);

    QList<QChar> Q65modes = {'D','E','F','G','H'};
    QList<QChar>::iterator it = Q65modes.begin();
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
  }

#if (QT_VERSION < QT_VERSION_CHECK(5, 15, 0))
  //VHF
  connect(modeButtonGroup, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked){
    qDebug() << "id" << id  << "toggled:" << checked;
    QString theBand = getBand();
    QChar theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  connect(bandButtonGroup, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked){
    qDebug() << "id" << id  << "toggled:" << checked;
    QString theBand = getBand();
    if((theBand=="A" || theBand=="B" || theBand=="C" || theBand=="D" || theBand=="E")) setupfmSpinBox(theBand);
    QChar theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });
  //HF
  connect(modeButtonGroup2, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked){
    qDebug() << "id" << id  << "toggled:" << checked;
    QString theBand = getBand();
    QChar theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  connect(bandButtonGroup2, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked){
    qDebug() << "id" << id  << "toggled:" << checked;
    QString theBand = getBand();
    QChar theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });
  //EME
  connect(modeButtonGroup3, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked){
    qDebug() << "id" << id  << "toggled:" << checked;
    QString theBand = getBand();
    QChar theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  connect(bandButtonGroup3, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), [=](int id, bool checked){
    qDebug() << "id" << id  << "toggled:" << checked;
    QString theBand = getBand();
    QChar theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
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
    QChar theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    QSYMessageCreator::WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  QObject::connect(bandButtonGroup, &QButtonGroup::idToggled, [&](int id, bool checked) {
    qDebug() << "Button" << id << "toggled:" << checked;
    QString theBand = getBand();
    if((theBand=="A" || theBand=="B" || theBand=="C" || theBand=="D" || theBand=="E")) setupfmSpinBox(theBand);
    QChar theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    QSYMessageCreator::WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });
  //HF
  QObject::connect(modeButtonGroup2, &QButtonGroup::idToggled, [&](int id, bool checked) {
    qDebug() << "Button" << id << "toggled:" << checked;
    QString theBand = getBand();
    QChar theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    QSYMessageCreator::WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  QObject::connect(bandButtonGroup2, &QButtonGroup::idToggled, [&](int id, bool checked) {
    qDebug() << "Button" << id << "toggled:" << checked;
    QString theBand = getBand();
    QChar theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    QSYMessageCreator::WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });
  //EME
  QObject::connect(modeButtonGroup3, &QButtonGroup::idToggled, [&](int id, bool checked) {
    qDebug() << "Button" << id << "toggled:" << checked;
    QString theBand = getBand();
    QChar theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    QSYMessageCreator::WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  QObject::connect(bandButtonGroup3, &QButtonGroup::idToggled, [&](int id, bool checked) {
    qDebug() << "Button" << id << "toggled:" << checked;
    QString theBand = getBand();
    QChar theMode = getMode(theBand, configuration_->region());
    setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
    QSYMessageCreator::WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  });

  //General Messages
  QObject::connect(messageButtonGroup, &QButtonGroup::idToggled, [&](int id, bool checked) {
    qDebug() << "Button" << id << "toggled:" << checked;
    QString message = getGenMessage();
  });

#endif

  connect(ui->fmSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &QSYMessageCreator::onfmSpinBoxValueChanged); //VHF
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

void QSYMessageCreator::onfmSpinBoxValueChanged()
{
  QString theBand = getBand();
  QChar theMode = getMode(theBand, configuration_->region());
  QString message = WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  setkHzVHF(ui->kHzBox->value());
  WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
}

void QSYMessageCreator::setupfmSpinBox(QString band) {
  if ((band =="A" || band == "B" || band == "C" || band == "D" || band == "E") && (configuration_->region () > 0)) {
    ui->label_2_FM->setHidden(false);
    ui->fmSpinBox->setHidden(false);
    ui->label_2_FM->setText("Region " + QString::number(configuration_->region()) + " MHz");
    if (configuration_->region ()==2) {
      if(band=="A") {
        ui->fmSpinBox->setRange(50,53);
      } else if(band=="B") {
        ui->fmSpinBox->setRange(144,147);
      } else if(band=="C") {
        ui->fmSpinBox->setRange(222,223);
      } else if(band=="D") {
        ui->fmSpinBox->setRange(440,449);
      } else if(band=="E") {
        ui->fmSpinBox->setRange(1290,1299);
      }
    } else { //not region 2
      if(band=="A") {
        ui->fmSpinBox->setRange(50,53);
      } else if(band=="B") {
        ui->fmSpinBox->setRange(144,145);
      } else if(band=="C") {
        ui->label_2_FM->setHidden(true);
      } else if(band=="D") {
        ui->fmSpinBox->setRange(430,439);
      } else if(band=="E") {
        ui->fmSpinBox->setRange(1290,1299);
      }
    }
    auto it = MHzFreqMap.find(band);
    int defaultValue = -1;
    int value =  (it != MHzFreqMap.constEnd()) ? it.value() : defaultValue;
    if(value >=0) {
      ui->fmSpinBox->setValue(value);
    }
    else {
      ui->fmSpinBox->setValue(ui->fmSpinBox->minimum());
    }
  }
}

void QSYMessageCreator::setkHzBox(QString theBand, QChar theMode, int tabNum)
{
  QPair<QString,QChar> key(theBand,theMode);
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
    QMap<QPair<QString, QChar>, int>::const_iterator it = kHzFreqMap.begin();
    bool running = true;
    while (running && (it !=kHzFreqMap.end())) {
      QPair<QString,QChar> key = it.key();
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
  QChar theMode = getMode(theBand, configuration_->region());
  QString message = WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  setkHzVHF(ui->kHzBox->value());
  WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
}

void QSYMessageCreator::onkHzBox2ValueChanged()
{
  QString theBand = getBand();
  QChar theMode = getMode(theBand, configuration_->region());
  QString message = WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  setkHzHF(ui->kHzBox2->value());
  WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
}

void QSYMessageCreator::onkHzBox3ValueChanged()
{
  QString theBand = getBand();
  QChar theMode = getMode(theBand, configuration_->region());
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
  QChar theMode = getMode(theBand, configuration_->region());
  setkHzBox(theBand, theMode, ui->tabWidget->currentIndex());
  WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
}

void QSYMessageCreator::on_button1_clicked()
{
  QString theBand = getBand();
  QChar theMode = getMode(theBand, configuration_->region());
  QString message = WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  QPair<QString,QChar>key(theBand,theMode);
  kHzFreqMap.insert(key, ui->kHzBox->value());
  if(!theMode.isLetter() && (theBand=="A" || theBand=="B" || theBand=="C" || theBand=="D" || theBand=="E")) MHzFreqMap.insert(theBand, ui->fmSpinBox->value());
  Q_EMIT sendMessage(message);
}

void QSYMessageCreator::on_saveMHzButton_clicked()
{
  QString theBand = getBand();
  QChar theMode = getMode(theBand, configuration_->region());
  QPair<QString,QChar>key(theBand,theMode);
  kHzFreqMap.insert(key, ui->kHzBox->value());
  MHzFreqMap.insert(theBand, ui->fmSpinBox->value());
}

void QSYMessageCreator::on_saveMHzButton_2_clicked()
{
  QString theBand = getBand();
  QChar theMode = getMode(theBand, configuration_->region());
  QPair<QString,QChar>key(theBand,theMode);
  kHzFreqMap.insert(key, ui->kHzBox2->value());
}

void QSYMessageCreator::on_saveMHzButton_3_clicked()
{
  QString theBand = getBand();
  QChar theMode = getMode(theBand, configuration_->region());
  QPair<QString,QChar>key(theBand,theMode);
  kHzFreqMap.insert(key, ui->kHzBox3->value());
}

void QSYMessageCreator::on_button2_clicked()
{
  QString theBand = getBand();
  QChar theMode = getMode(theBand, configuration_->region());
  QString message = WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  QPair<QString,QChar>key(theBand,theMode);
  kHzFreqMap.insert(key, ui->kHzBox2->value());
  Q_EMIT sendMessage(message);
}

void QSYMessageCreator::on_button3_clicked()
{
  QString theBand = getBand();
  QChar theMode = getMode(theBand, configuration_->region());
  QString message = WriteMessage(ui->DxBaseLabel->text(), theBand, theMode);
  QPair<QString,QChar>key(theBand,theMode);
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
  message = ui->DxBaseLabel->text() + ".ZA" + message;
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
  QChar theMode = getMode(theBand, configuration_->region());
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

  QByteArray readKHzData = settings_->value("kHzFreqMap").toByteArray();
  QDataStream readKHzStream(&readKHzData, QIODevice::ReadOnly);
  readKHzStream >> kHzFreqMap;
  QByteArray readMHzData = settings_->value("MHzFreqMap").toByteArray();
  QDataStream readMHzStream(&readMHzData, QIODevice::ReadOnly);
  readMHzStream >> MHzFreqMap;

  ui->kHzBox3->setValue(settings_->value("kHzEME").toInt());
  setbandEME(settings_->value("bandEME").toString());
  setmodeEME(settings_->value("modeEME").toChar());
  ui->kHzBox->setValue(settings_->value("kHzVHF").toInt());
  setbandVHF(settings_->value("bandVHF").toString());
  setmodeVHF(settings_->value("modeVHF").toChar());
  initVHFMode = settings_->value("modeVHF").toChar();
  ui->kHzBox2->setValue(settings_->value("kHzHF").toInt());
  setbandHF(settings_->value("bandHF").toString());
  setmodeHF(settings_->value("modeHF").toChar());
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
    setmodeVHF('V');
    testVHF=false;
  }
  if ((settings_->value("bandHF").toString()).compare("")==0) {
    setbandHF("M");
    testHF = false;
  }
  if ((settings_->value("modeHF").toString()).compare("")==0) {
    setmodeHF('V');
    testHF = false;
  }
  if ((settings_->value("bandEME").toString()).compare("")==0) {
    setbandEME("A");
    testEME = false;
  }
  if ((settings_->value("modeEME").toString()).compare("")==0) {
    setmodeEME('V');
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
    QPair<QString,QChar> key(getbandVHF(),getmodeVHF());
    kHzFreqMap.insert(key,getkHzVHF());
    }

  if (testEME) {
    QPair<QString,QChar> key(getbandEME(),getmodeEME());
    kHzFreqMap.insert(key,getkHzEME());
  }

  if (testHF) {
    QPair<QString,QChar> key(getbandHF(),getmodeHF());
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

QString QSYMessageCreator::WriteMessage (QString hisCall, QString band, QChar mode)
{
  QString message = "";
  if(ui->tabWidget->currentIndex() == 1) {
    qint16 kHzFreq = ui->kHzBox->value();
    QString kHzStr = QStringLiteral("%1").arg(kHzFreq,3,10,QLatin1Char('0'));
    message = hisCall + "." + band + mode + kHzStr;
    ui->messageLabel->setText(message);
  }
  else if(ui->tabWidget->currentIndex() == 0) {
    qint16 kHzFreq = ui->kHzBox2->value();
    QString kHzStr = QStringLiteral("%1").arg(kHzFreq,3,10,QLatin1Char('0'));
    message = hisCall + "." + band + mode + kHzStr;
    ui->messageLabel2->setText(message);
  }
  else if(ui->tabWidget->currentIndex() == 2) {
    qint16 kHzFreq = ui->kHzBox3->value();
    QString kHzStr = QStringLiteral("%1").arg(kHzFreq,3,10,QLatin1Char('0'));
    message = hisCall + "." + band + mode + kHzStr;
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

void QSYMessageCreator::setMode(QString band, QChar mode, int region)
{
  if(ui->tabWidget->currentIndex() == 1) { //VHF tab
    if (mode=='V') {  //SSB
      ui->radioButSSB->setChecked(true);
      ui->label_2_FM->setHidden(true);
      ui->fmSpinBox->setHidden(true);
    }
    else if (mode=='W') {  //CW
      ui->radioButCW->setChecked(true);
      ui->label_2_FM->setHidden(true);
      ui->fmSpinBox->setHidden(true);
    }
    else if (mode=='K') {  //MSK144
      ui->radioButMSK->setChecked(true);
      ui->label_2_FM->setHidden(true);
      ui->fmSpinBox->setHidden(true);
    }
    else if (mode=='L') { //FT8
      ui->radioButFT8->setChecked(true);
      ui->label_2_FM->setHidden(true);
      ui->fmSpinBox->setHidden(true);
    }
    else if (!mode.isLetter()) {
      ui->radioButFM->setChecked(true);
      if ((band =="A" || band == "B" || band == "C" || band == "D" || band == "E") && (configuration_->region () > 0)) {
        ui->label_2_FM->setHidden(false);
        ui->fmSpinBox->setHidden(false);
        ui->label_2_FM->setText("Region " + QString::number(configuration_->region()) + " MHz");
        if (region==2) {
          if(band=="A") {
            ui->fmSpinBox->setRange(50,53);
            if(mode=='0') ui->fmSpinBox->setValue(50);
            if(mode=='1') ui->fmSpinBox->setValue(51);
            if(mode=='2') ui->fmSpinBox->setValue(52);
            if(mode=='3') ui->fmSpinBox->setValue(53);
          } else if(band=="B") {
            ui->fmSpinBox->setRange(144,147);
            if(mode=='4') ui->fmSpinBox->setValue(144);
            if(mode=='5') ui->fmSpinBox->setValue(145);
            if(mode=='6') ui->fmSpinBox->setValue(146);
            if(mode=='7') ui->fmSpinBox->setValue(147);
          } else if(band=="C") {
            ui->fmSpinBox->setRange(222,223);
            if(mode=='2') ui->fmSpinBox->setValue(222);
            if(mode=='3') ui->fmSpinBox->setValue(223);
          } else if(band=="D") {
            ui->fmSpinBox->setRange(440,449);
            if(mode=='0') ui->fmSpinBox->setValue(440);
            if(mode=='1') ui->fmSpinBox->setValue(441);
            if(mode=='2') ui->fmSpinBox->setValue(442);
            if(mode=='3') ui->fmSpinBox->setValue(443);
            if(mode=='4') ui->fmSpinBox->setValue(444);
            if(mode=='5') ui->fmSpinBox->setValue(445);
            if(mode=='6') ui->fmSpinBox->setValue(446);
            if(mode=='7') ui->fmSpinBox->setValue(447);
            if(mode=='8') ui->fmSpinBox->setValue(448);
            if(mode=='9') ui->fmSpinBox->setValue(449);
          } else if(band=="E") {
            ui->fmSpinBox->setRange(1290,1299);
            if(mode=='0') ui->fmSpinBox->setValue(1290);
            if(mode=='1') ui->fmSpinBox->setValue(1291);
            if(mode=='2') ui->fmSpinBox->setValue(1292);
            if(mode=='3') ui->fmSpinBox->setValue(1293);
            if(mode=='4') ui->fmSpinBox->setValue(1294);
            if(mode=='5') ui->fmSpinBox->setValue(1295);
            if(mode=='6') ui->fmSpinBox->setValue(1296);
            if(mode=='7') ui->fmSpinBox->setValue(1297);
            if(mode=='8') ui->fmSpinBox->setValue(1298);
            if(mode=='9') ui->fmSpinBox->setValue(1299);
          }
        } else { //not region 2
          if(band=="A") {
            ui->fmSpinBox->setRange(50,53);
            if(mode=='0') ui->fmSpinBox->setValue(50);
            if(mode=='1') ui->fmSpinBox->setValue(51);
            if(mode=='2') ui->fmSpinBox->setValue(52);
            if(mode=='3') ui->fmSpinBox->setValue(53);
          } else if(band=="B") {
            ui->fmSpinBox->setRange(144,145);
            if(mode=='4') ui->fmSpinBox->setValue(144);
            if(mode=='5') ui->fmSpinBox->setValue(145);
          } else if(band=="C") {
            ui->label_2_FM->setHidden(true);
          } else if(band=="D") {
            ui->fmSpinBox->setRange(430,439);
            if(mode=='0') ui->fmSpinBox->setValue(430);
            if(mode=='1') ui->fmSpinBox->setValue(431);
            if(mode=='2') ui->fmSpinBox->setValue(432);
            if(mode=='3') ui->fmSpinBox->setValue(433);
            if(mode=='4') ui->fmSpinBox->setValue(434);
            if(mode=='5') ui->fmSpinBox->setValue(435);
            if(mode=='6') ui->fmSpinBox->setValue(436);
            if(mode=='7') ui->fmSpinBox->setValue(437);
            if(mode=='8') ui->fmSpinBox->setValue(438);
            if(mode=='9') ui->fmSpinBox->setValue(439);
          } else if(band=="E") {
            ui->fmSpinBox->setRange(1290,1299);
            if(mode=='0') ui->fmSpinBox->setValue(1290);
            if(mode=='1') ui->fmSpinBox->setValue(1291);
            if(mode=='2') ui->fmSpinBox->setValue(1292);
            if(mode=='3') ui->fmSpinBox->setValue(1293);
            if(mode=='4') ui->fmSpinBox->setValue(1294);
            if(mode=='5') ui->fmSpinBox->setValue(1295);
            if(mode=='6') ui->fmSpinBox->setValue(1296);
            if(mode=='7') ui->fmSpinBox->setValue(1297);
            if(mode=='8') ui->fmSpinBox->setValue(1298);
            if(mode=='9') ui->fmSpinBox->setValue(1299);
          }
        }
      } else {  // is not lower 4 bands FM Mode
        ui->label_2_FM->setHidden(true);
        ui->fmSpinBox->setHidden(true);
      }      
      QSYMessageCreator::MHzFreqMap.insert(band,ui->fmSpinBox->value());
    } //end mode !isLetter ( is FM )
  } //end vhf tab
  else if(ui->tabWidget->currentIndex() == 0) {  //HF tab
    if (mode=='V') {
      ui->radioButSSB2->setChecked(true);
    }
    else if (mode=='W') {
      ui->radioButCW2->setChecked(true);
    }
    else if (!mode.isLetter()) {
      ui->radioButFM2->setChecked(true);
    }
    else if (mode=='L') {
      ui->radioButFT82->setChecked(true);
    }
    else if (mode=='J') {
      ui->radioButFT4->setChecked(true);
    }
    else if (mode=='K') {
        ui->radioButMSK->setChecked(true);
    }
    else if (mode=='A') {
      ui->radioButJT9->setChecked(true);
    }
    else if (mode=='B') {
      ui->radioButJT65->setChecked(true);
    }
    else if (mode=='C') {
      ui->radioButFST4->setChecked(true);
    }
  } else if(ui->tabWidget->currentIndex() == 2) { //EME tab
    if (mode=='D') {
      ui->radioButQ6530B->setChecked(true);
    }
    else if (mode=='W') {
      ui->radioButCW3->setChecked(true);
    }
    else if (mode=='E') {
      ui->radioButQ6560C->setChecked(true);
    }
    else if (mode=='F') {
      ui->radioButQ6560D->setChecked(true);
    }
    else if (mode=='G') {
      ui->radioButQ6560E->setChecked(true);
    }
    else if (mode=='H') {
      ui->radioButQ65120D->setChecked(true);
    }
  }
}

QChar QSYMessageCreator::modeFromSpinBox(QString band, int MHzVHFInt)
{
  QChar mode = '0';
  if(band=="A") {
    if(MHzVHFInt == 50) mode = '0';
    else if(MHzVHFInt == 51) mode = '1';
    else if(MHzVHFInt == 52) mode = '2';
    else if(MHzVHFInt == 53) mode = '3';
  } else if(band=="B") {
    if(MHzVHFInt == 144) mode = '4';
    else if(MHzVHFInt == 145) mode = '5';
    else if(MHzVHFInt == 146) mode = '6';
    else if(MHzVHFInt == 147) mode = '7';
  } else if(band=="C") {
    if(configuration_->region() ==2) {
      if(MHzVHFInt == 222) mode = '2';
       else if(MHzVHFInt == 223) mode = '3';
    }
    else { //not region 2
      mode = '0';
    }
  } else if(band=="D") {
    if(configuration_->region() ==2) {
      if(MHzVHFInt == 440) mode = '0';
      else if(MHzVHFInt == 441) mode = '1';
      else if(MHzVHFInt == 442) mode = '2';
      else if(MHzVHFInt == 443) mode = '3';
      else if(MHzVHFInt == 444) mode = '4';
      else if(MHzVHFInt == 445) mode = '5';
      else if(MHzVHFInt == 446) mode = '6';
      else if(MHzVHFInt == 447) mode = '7';
      else if(MHzVHFInt == 448) mode = '8';
      else if(MHzVHFInt == 449) mode = '9';
    } else { //not region 2
      if(MHzVHFInt == 430) mode = '0';
      else if(MHzVHFInt == 431) mode = '1';
      else if(MHzVHFInt == 432) mode = '2';
      else if(MHzVHFInt == 433) mode = '3';
      else if(MHzVHFInt == 434) mode = '4';
      else if(MHzVHFInt == 435) mode = '5';
      else if(MHzVHFInt == 436) mode = '6';
      else if(MHzVHFInt == 437) mode = '7';
      else if(MHzVHFInt == 438) mode = '8';
      else if(MHzVHFInt == 439) mode = '9';
    }
  } else if(band=="E") {
    if(MHzVHFInt == 1290) mode = '0';
    else if(MHzVHFInt == 1291) mode = '1';
    else if(MHzVHFInt == 1292) mode = '2';
    else if(MHzVHFInt == 1293) mode = '3';
    else if(MHzVHFInt == 1294) mode = '4';
    else if(MHzVHFInt == 1295) mode = '5';
    else if(MHzVHFInt == 1296) mode = '6';
    else if(MHzVHFInt == 1297) mode = '7';
    else if(MHzVHFInt == 1298) mode = '8';
    else if(MHzVHFInt == 1299) mode = '9';
  }
  return mode;
}

QChar QSYMessageCreator::getMode(QString band,int region)
{
  QChar mode;
  qDebug() << "band" << band;
  qDebug() << "band" << region;

  if(ui->tabWidget->currentIndex() == 1) {    
    ui->label_2_FM->setHidden(true);
    ui->fmSpinBox->setHidden(true);
    if (ui->radioButFM->isChecked()) {
      if ((band =="A" || band == "B" || band == "C" || band == "D" || band == "E") && (configuration_->region () > 0)) {
        if(configuration_->region () !=2 &&(band=="C"))  {
          ui->label_2_FM->setHidden(true);
          ui->fmSpinBox->setHidden(true);
          mode = '0';
        } else {
          ui->label_2_FM->setText("Region " + QString::number(configuration_->region()) + " MHz");
          ui->label_2_FM->setHidden(false);
          ui->fmSpinBox->setHidden(false);
          if((ui->fmSpinBox->text().toInt() < getBandEdge(band)) || ((band=="A" ) && (initVHFMode.isLetter()))) {
            initVHFMode='0';
            setupfmSpinBox(band);
            if(firstTime) {
              firstTime = false;
              if(region ==2) {
                if(band=="A") {
                  ui->fmSpinBox->setRange(50,53);
                  ui->fmSpinBox->setValue(52);
                  ui->kHzBox->setValue(525);
                } else if(band=="B") {
                  ui->fmSpinBox->setRange(144,147);
                  ui->fmSpinBox->setValue(146);
                  ui->kHzBox->setValue(520);
                } else if(band=="C") {
                  ui->fmSpinBox->setRange(222,223);
                  ui->fmSpinBox->setValue(223);
                  ui->kHzBox->setValue(500);
                } else if(band=="D") {
                  ui->fmSpinBox->setRange(440,449);
                  ui->fmSpinBox->setValue(446);
                  ui->kHzBox->setValue(000);
                } else if(band=="E") {
                  ui->fmSpinBox->setRange(1290,1299);
                  ui->fmSpinBox->setValue(1294);
                  ui->kHzBox->setValue(000);
                }
              }  else {
                if(band=="A") {
                  ui->fmSpinBox->setRange(50,53);
                  ui->fmSpinBox->setValue(50);
                  ui->kHzBox->setValue(800);
                } else if(band=="B") {
                  ui->fmSpinBox->setRange(144,145);
                  ui->fmSpinBox->setValue(145);
                  ui->kHzBox->setValue(500);
                } else if(band=="D") {
                  ui->fmSpinBox->setRange(430,439);
                  ui->fmSpinBox->setValue(433);
                  ui->kHzBox->setValue(650);
                } else if(band=="E") {
                  ui->fmSpinBox->setRange(1290,1299);
                  ui->fmSpinBox->setValue(1297);
                  ui->kHzBox->setValue(500);
                }
              }
            }
          }
          mode = modeFromSpinBox(band, ui->fmSpinBox->value());
        }
      } else {
        ui->label_2_FM->setHidden(true);
        ui->fmSpinBox->setHidden(true);
        mode='0';
      }
    }
    else if (ui->radioButSSB->isChecked()) {
        mode = 'V';
    }
    else if (ui->radioButCW->isChecked()) {
      mode = 'W';
    }
    else if (ui->radioButFT8->isChecked()) {
      mode = 'L';
    }
    else if (ui->radioButMSK->isChecked()) {
      mode = 'K';
    } else mode = 'V';
    setmodeVHF(mode);
  } else if(ui->tabWidget->currentIndex() == 0) {
    if (ui->radioButSSB2->isChecked()) {
      mode = 'V';
    }
    else if (ui->radioButFM2->isChecked()) {
      mode = '0';
    }
    else if (ui->radioButCW2->isChecked()) {
      mode = 'W';
    }
    else if (ui->radioButFT82->isChecked()) {
      mode = 'L';
    }
    else if (ui->radioButFT4->isChecked()) {
      mode = 'J';
    }
    else if (ui->radioButJT9->isChecked()) {
      mode = 'A';
    }
    else if (ui->radioButJT65->isChecked()) {
      mode = 'B';
    }
    else if (ui->radioButFST4->isChecked()) {
      mode = 'C';
    } else mode = 'V';
    setmodeHF(mode);
  } else if(ui->tabWidget->currentIndex() == 2) {
    if (ui->radioButQ6530B->isChecked()) {
      mode = 'D';
    }
    else if (ui->radioButQ6560C->isChecked()) {
      mode = 'E';
    }
    else if (ui->radioButCW3->isChecked()) {
      mode = 'W';
    }
    else if (ui->radioButQ6560D->isChecked()) {
      mode = 'F';
    }
    else if (ui->radioButQ6560E->isChecked()) {
      mode = 'G';
    }
    else if (ui->radioButQ65120D->isChecked()) {
      mode = 'H';
    } else mode = 'V';
    setmodeEME(mode);
  }
  return mode;
}

int QSYMessageCreator::getBandEdge(QString band) {
  if(band=="A") return 50;
  else if(band=="B") return 144;
  else if(band=="C") return 222;
  else if(band=="D") return 430;
  else if(band=="E") return 1290;
  else return 50;
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
  QByteArray data;
  QDataStream *stream = new QDataStream(&data, QIODevice::WriteOnly);
  *stream << kHzFreqMap;
  settings_->setValue("kHzFreqMap", data);
  QByteArray data2;
  QDataStream *stream2 = new QDataStream(&data2, QIODevice::WriteOnly);
  *stream2 << MHzFreqMap;
  settings_->setValue("MHzFreqMap", data2);
  delete stream;
  delete stream2;
}
