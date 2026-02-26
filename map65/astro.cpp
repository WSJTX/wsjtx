#include "astro.h"
#include <QSettings>
#include "ui_astro.h"
#include <QDebug>
#include <QFile>
#include <QMessageBox>
#include <stdio.h>
#include "SettingsGroup.hpp"
#include "commons.h"
#include <math.h>

extern "C" {
  void astrosub_ (int* nyear, int* month, int* nday, double* uth, int* nfreq,
                  const char* mygrid, const char* hisgrid, double* azsun,
                  double* elsun, double* azmoon, double* elmoon, double* azmoondx,
                  double* elmoondx, int* ntsky, int* ndop, int* ndop00,
                  double* ramoon, double* decmoon, double* dgrd, double* poloffset,
                  double* xnr, int len1, int len2);
}

Astro::Astro (QString const& settings_filename, QWidget *parent) :
  QWidget(parent),
  ui(new Ui::Astro),
  m_settings_filename {settings_filename}
{
  ui->setupUi (this);
  setWindowTitle ("Astronomical Data");
  setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint);
  QSettings settings {m_settings_filename, QSettings::IniFormat};
  SettingsGroup g {&settings, "MainWindow"}; // MainWindow group for
                                             // historical reasons
  setGeometry (settings.value ("AstroGeom", QRect {71, 390, 227, 403}).toRect ());
  ui->astroTextBrowser->setStyleSheet(
        "QTextBrowser { background-color : cyan; color : black; }");
  ui->astroTextBrowser->clear();
  m_AzElDir0="";
}

Astro::~Astro()
{
  QSettings settings {m_settings_filename, QSettings::IniFormat};
  SettingsGroup g {&settings, "MainWindow"};
  settings.setValue ("AstroGeom", geometry ());
  delete ui;
}

void Astro::astroUpdate(QDateTime t, QString mygrid, QString hisgrid,
                        int fQSO, int nsetftx, int ntxFreq, QString azelDir, double xavg)
{
  static int ntxFreq0=-99;
  char cc[300];
  double azsun,elsun,azmoon,elmoon,azmoondx,elmoondx;
  double ramoon,decmoon,dgrd,poloffset,xnr;
  int ntsky,ndop,ndop00;
  QString date = t.date().toString("yyyy MMM dd");
  QString utc = t.time().toString();
  int nyear=t.date().year();
  int month=t.date().month();
  int nday=t.date().day();
  int nhr=t.time().hour();
  int nmin=t.time().minute();
  double sec=t.time().second() + 0.001*t.time().msec();
  int isec=sec;
  double uth=nhr + nmin/60.0 + sec/3600.0;
  int nfreq=(int)datcom_.fcenter;
//  if(nfreq<10 or nfreq > 50000) nfreq=144;

  astrosub_(&nyear, &month, &nday, &uth, &nfreq, mygrid.toLatin1(),
            hisgrid.toLatin1(), &azsun, &elsun, &azmoon, &elmoon,
            &azmoondx, &elmoondx, &ntsky, &ndop, &ndop00,&ramoon, &decmoon,
            &dgrd, &poloffset, &xnr, 6, 6);

  datcom_.nfast=ndop00;               //Send self Doppler to decoder, via datcom
  sprintf(cc,
          "Az:    %6.1f\n"
          "El:    %6.1f\n"
          "MyDop: %6d\n"
          "DxAz:  %6.1f\n"
          "DxEl:  %6.1f\n"
          "DxDop: %6d\n"
          "Dec:   %6.1f\n"
          "SunAz: %6.1f\n"
          "SunEl: %6.1f\n"
          "Tsky:  %6d\n"
          "MNR:   %6.1f\n"
          "Dgrd:  %6.1f",
          azmoon,elmoon,ndop00,azmoondx,elmoondx,ndop,decmoon,azsun,elsun,
          ntsky,xnr,dgrd);
  ui->astroTextBrowser->setText(" "+ date + "\nUTC: " + utc + "\n" + cc);

  double azOffset=0.0;
  double elOffset=0.0;
  double rad=57.2957795131;
  int iCycle=2;
// Are we doing pointing tests?
  bool bPointing=ui->cbPointingTests->isChecked();
  ui->gbPointing->setVisible(bPointing);
  if(bPointing) {
    int nDwell=int(ui->sbDwell->value());
    if(ui->cbAutoCycle->isChecked()) {
      iCycle=(t.currentSecsSinceEpoch()%(6*nDwell))/nDwell + 1;
      if(iCycle==1) {
        azOffset = -ui->sbOffset->value()/cos(elsun/rad);
        ui->rb1->setChecked(true);
      }
      if(iCycle==2 or iCycle==5) {
        ui->rb2->setChecked(true);
      }
      if(iCycle==3) {
        azOffset = +ui->sbOffset->value()/cos(elsun/rad);
        ui->rb3->setChecked(true);
      }
      if(iCycle==4) {
        elOffset = -ui->sbOffset->value();
        ui->rb4->setChecked(true);
      }
      if(iCycle==6) {
        elOffset = +ui->sbOffset->value();
        ui->rb6->setChecked(true);
      }
    }
    if(ui->cbOnOff->isChecked()) {
      iCycle=(t.currentSecsSinceEpoch()%(2*nDwell))/nDwell + 1;
      if(iCycle==1) {
        azOffset = -ui->sbOffset->value()/cos(elsun/rad);
        ui->rb1->setChecked(true);
      }
      if(iCycle==2) {
        ui->rb2->setChecked(true);
      }
    }
    if(ui->cbAutoCycle->isChecked() or ui->cbOnOff->isChecked()) {
      QFile f("pointing.out");
      if(f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        QTextStream out(&f);
        out << t.toString("yyyy-MMM-dd hh:mm:ss");
        sprintf(cc,"%7.1f %7.1f   %d %7.1f %7.1f %10.1f %7.2f\n",
                azsun,elsun,iCycle,azOffset,elOffset,xavg,10.0*log10(xavg));
        out << cc;
        f.close();
      }
    }
  } else {
    ui->rb2->setChecked(true);
    ui->cbAutoCycle->setChecked(false);
    ui->cbOnOff->setChecked(false);
  }

// Write pointing data to azel.dat
  QString fname=azelDir+"/azel.dat";
  QFile f(fname);
  if(!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if(azelDir==m_AzElDir0) return;
    m_AzElDir0=azelDir;
    QMessageBox mb;
    mb.setText("Cannot open " + fname + "\nCorrect the setting of AzEl Directory in Setup?");
    mb.exec();
    return;
  }
  int ndiff=0;
  if(ntxFreq != ntxFreq0) ndiff=1;
  ntxFreq0=ntxFreq;
  QTextStream out(&f);
  sprintf(cc,"%2.2d:%2.2d:%2.2d,%5.1f,%5.1f,Moon\n"
          "%2.2d:%2.2d:%2.2d,%5.1f,%5.1f,Sun\n"
          "%2.2d:%2.2d:%2.2d,%5.1f,%5.1f,Source\n"
          "%4d,%6d,%6d,Doppler\n"
          "%3d,%1d,fQSO\n"
          "%3d,%1d,fQSO2\n",
          nhr,nmin,isec,azmoon,elmoon,
          nhr,nmin,isec,azsun+azOffset,elsun+elOffset,
          nhr,nmin,isec,0.0,0.0,
          nfreq,ndop,ndop00,
          fQSO,nsetftx,
          ntxFreq,ndiff);
  out << cc;
  f.close();
}

void Astro::setFontSize(int n)
{
  ui->astroTextBrowser->setFontPointSize(n);
}

void Astro::on_cbAutoCycle_clicked(bool checked)
{
  if(checked) ui->cbOnOff->setChecked(false);
}

void Astro::on_cbOnOff_clicked(bool checked)
{
  if(checked) ui->cbAutoCycle->setChecked(false);
}

