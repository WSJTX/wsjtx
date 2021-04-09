#include "astro.h"
#include "ui_astro.h"
#include <QDebug>
#include <QFile>
#include <QMessageBox>
#include <stdio.h>
#include "commons.h"

Astro::Astro(QWidget *parent) :
  QWidget(parent),
  ui(new Ui::Astro)
{
  ui->setupUi(this);
  ui->astroTextBrowser->setStyleSheet(
        "QTextBrowser { background-color : cyan; color : black; }");
  ui->astroTextBrowser->clear();
  m_AzElDir0="";
}

Astro::~Astro()
{
    delete ui;
}

void Astro::astroUpdate(QDateTime t, QString mygrid, QString hisgrid,
                        int fQSO, int nsetftx, int ntxFreq, QString azelDir)
{
  static int ntxFreq0=-99;
  static bool astroBusy=false;
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
  if(nfreq<10 or nfreq > 50000) nfreq=144;

  if(!astroBusy) {
    astroBusy=true;
    astrosub_(&nyear, &month, &nday, &uth, &nfreq, mygrid.toLatin1(),
            hisgrid.toLatin1(), &azsun, &elsun, &azmoon, &elmoon,
            &azmoondx, &elmoondx, &ntsky, &ndop, &ndop00,&ramoon, &decmoon,
            &dgrd, &poloffset, &xnr, 6, 6);
    astroBusy=false;
  }

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
          "%4d,%6d,Doppler\n"
          "%3d,%1d,fQSO\n"
          "%3d,%1d,fQSO2\n",
          nhr,nmin,isec,azmoon,elmoon,
          nhr,nmin,isec,azsun,elsun,
          nhr,nmin,isec,0.0,0.0,
          nfreq,ndop,
          fQSO,nsetftx,
          ntxFreq,ndiff);
  out << cc;
  f.close();
}

void Astro::setFontSize(int n)
{
  ui->astroTextBrowser->setFontPointSize(n);
}
