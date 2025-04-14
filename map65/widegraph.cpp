#include "widegraph.h"
#include <QSettings>
#include <QMessageBox>
#include "SettingsGroup.hpp"
#include "ui_widegraph.h"

#define NFFT 32768

WideGraph::WideGraph (QString const& settings_filename, QWidget * parent)
  : QDialog {parent},
    ui {new Ui::WideGraph},
    m_settings_filename {settings_filename}
{
  ui->setupUi(this);
  setWindowTitle("Wide Graph");
  setWindowFlags(Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint);
  installEventFilter(parent); //Installing the filter
  ui->widePlot->setCursor(Qt::CrossCursor);
  setMaximumWidth(2048);
  setMaximumHeight(880);
  ui->widePlot->setMaximumHeight(800);
  m_bIQxt=false;
  connect(ui->widePlot, SIGNAL(freezeDecode1(int)),this,
          SLOT(wideFreezeDecode(int)));

  //Restore user's settings
  QSettings settings {m_settings_filename, QSettings::IniFormat};
  {
    SettingsGroup g {&settings, "MainWindow"}; // historical reasons
    setGeometry (settings.value ("WideGraphGeom", QRect {45,30,1023,340}).toRect ());
  }
  SettingsGroup g {&settings, "WideGraph"};
  ui->widePlot->setPlotZero(settings.value("PlotZero", 20).toInt());
  ui->widePlot->setPlotGain(settings.value("PlotGain", 0).toInt());
  ui->zeroSpinBox->setValue(ui->widePlot->getPlotZero());
  ui->gainSpinBox->setValue(ui->widePlot->getPlotGain());
  int n = settings.value("FreqSpan",60).toInt();
  int w = settings.value("PlotWidth",1000).toInt();
  ui->freqSpanSpinBox->setValue(n);
  ui->widePlot->setNSpan(n);
  int nbpp = n * 32768.0/(w*96.0) + 0.5;
  ui->widePlot->setBinsPerPixel(nbpp);
  m_waterfallAvg = settings.value("WaterfallAvg",10).toInt();
  ui->waterfallAvgSpinBox->setValue(m_waterfallAvg);
  ui->freqOffsetSpinBox->setValue(settings.value("FreqOffset",0).toInt());
  m_bForceCenterFreq=settings.value("ForceCenterFreqBool",false).toBool();
  m_dForceCenterFreq=settings.value("ForceCenterFreqMHz",144.125).toDouble();
  ui->cbFcenter->setChecked(m_bForceCenterFreq);
  ui->cbLockTxRx->setChecked(m_bLockTxRx);
  ui->fCenterLineEdit->setText(QString::number(m_dForceCenterFreq));
  m_bLockTxRx=settings.value("LockTxRx",false).toBool();
  ui->cbLockTxRx->setChecked(m_bLockTxRx);
}

WideGraph::~WideGraph()
{
  saveSettings();
  delete ui;
}

void WideGraph::resizeEvent(QResizeEvent* )                    //resizeEvent()
{
  if(!size().isValid()) return;
  int w = size().width();
  int h = size().height();
  ui->labFreq->setGeometry(QRect(w-256,h-100,227,41));
}

void WideGraph::saveSettings()
{
  //Save user's settings
  QSettings settings {m_settings_filename, QSettings::IniFormat};
  {
    SettingsGroup g {&settings, "MainWindow"}; // for historical reasons
    settings.setValue ("WideGraphGeom", geometry());
  }
  SettingsGroup g {&settings, "WideGraph"};
  settings.setValue("PlotZero",ui->widePlot->m_plotZero);
  settings.setValue("PlotGain",ui->widePlot->m_plotGain);
  settings.setValue("PlotWidth",ui->widePlot->plotWidth());
  settings.setValue("FreqSpan",ui->freqSpanSpinBox->value());
  settings.setValue("WaterfallAvg",ui->waterfallAvgSpinBox->value());
  settings.setValue("FreqOffset",ui->widePlot->freqOffset());
  settings.setValue("ForceCenterFreqBool",m_bForceCenterFreq);
  settings.setValue("ForceCenterFreqMHz",m_dForceCenterFreq);
  settings.setValue("LockTxRx",m_bLockTxRx);
}

void WideGraph::dataSink2(float s[], int nkhz, int ihsym, int ndiskdata,
                          uchar lstrong[])
{
  static float splot[NFFT];
  float swide[2048];
  float smax;
  double df;
  int nbpp = ui->widePlot->binsPerPixel();
  static int n=0;
  static int nkhz0=-999;
  static int ntrz=0;

  df = m_fSample/32768.0;
  if(nkhz != nkhz0) {
    ui->widePlot->setNkhz(nkhz);                   //Why do we need both?
    ui->widePlot->SetCenterFreq(nkhz);             //Why do we need both?
    ui->widePlot->setFQSO(nkhz,true);
    nkhz0 = nkhz;
  }

  //Average spectra over specified number, m_waterfallAvg
  if (n==0) {
    for (int i=0; i<NFFT; i++)
      splot[i]=s[i];
  } else {
    for (int i=0; i<NFFT; i++)
      splot[i] += s[i];
  }
  n++;

  if (n>=m_waterfallAvg) {
    for (int i=0; i<NFFT; i++)
        splot[i] /= n;                       //Normalize the average
    n=0;

    int w=ui->widePlot->plotWidth();
    qint64 sf = nkhz + ui->widePlot->freqOffset() - 0.5*w*nbpp*df/1000.0;
    if(sf != ui->widePlot->startFreq()) ui->widePlot->SetStartFreq(sf);
    int i0=16384.0+(ui->widePlot->startFreq()-nkhz+1.27046+0.001*m_fCal) *
        1000.0/df + 0.5;
    int i=i0;
    for (int j=0; j<2048; j++) {
        smax=0;
        for (int k=0; k<nbpp; k++) {
            i++;
            if(splot[i]>smax) smax=splot[i];
        }
        swide[j]=smax;
        if(lstrong[1 + i/32]!=0) swide[j]=-smax;   //Tag strong signals
    }

// Time according to this computer
    qint64 ms = QDateTime::currentMSecsSinceEpoch() % 86400000;
    int ntr = (ms/1000) % m_TRperiod;

    if((ndiskdata && ihsym <= m_waterfallAvg) || (!ndiskdata && ntr<ntrz)) {
      for (int i=0; i<2048; i++) {
        swide[i] = 1.e30;
      }
      for (int i=0; i<32768; i++) {
        splot[i] = 1.e30;
      }
    }
    ntrz=ntr;
    ui->widePlot->draw(swide,i0,splot);
  }
}

void WideGraph::on_freqOffsetSpinBox_valueChanged(int f)
{
  ui->widePlot->SetFreqOffset(f);
}

void WideGraph::on_freqSpanSpinBox_valueChanged(int n)
{
  ui->widePlot->setNSpan(n);
  int w = ui->widePlot->plotWidth();
  int nbpp = n * 32768.0/(w*96.0) + 0.5;
  if(nbpp < 1) nbpp=1;
  if(w > 0) {
    ui->widePlot->setBinsPerPixel(nbpp);
  }
}

void WideGraph::on_waterfallAvgSpinBox_valueChanged(int n)
{
  m_waterfallAvg = n;
}

void WideGraph::on_zeroSpinBox_valueChanged(int value)
{
  ui->widePlot->setPlotZero(value);
}

void WideGraph::on_gainSpinBox_valueChanged(int value)
{
  ui->widePlot->setPlotGain(value);
}

void WideGraph::keyPressEvent(QKeyEvent *e)
{  
  switch(e->key())
  {
  case Qt::Key_F11:
    emit f11f12(11);
    break;
  case Qt::Key_F12:
    emit f11f12(12);
    break;
  default:
    e->ignore();
  }
}

int WideGraph::QSOfreq()
{
  return ui->widePlot->fQSO();
}

int WideGraph::nSpan()
{
  return ui->widePlot->m_nSpan;
}

float WideGraph::fSpan()
{
  return ui->widePlot->m_fSpan;
}

int WideGraph::nStartFreq()
{
  return ui->widePlot->startFreq();
}

void WideGraph::wideFreezeDecode(int n)
{
  emit freezeDecode2(n);
}

void WideGraph::setTol(int n)
{
  ui->widePlot->m_tol=n;
  ui->widePlot->DrawOverlay();
  ui->widePlot->update();
}

int WideGraph::Tol()
{
  return ui->widePlot->m_tol;
}

void WideGraph::setDF(int n)
{
  ui->widePlot->m_DF=n;
  ui->widePlot->DrawOverlay();
  ui->widePlot->update();
}

void WideGraph::setFcal(int n)
{
  m_fCal=n;
  ui->widePlot->setFcal(n);
}

void WideGraph::setDecodeFinished()
{
  ui->widePlot->DecodeFinished();
}

int WideGraph::DF()
{
  return ui->widePlot->m_DF;
}

void WideGraph::on_autoZeroPushButton_clicked()
{
   int nzero=ui->widePlot->autoZero();
   ui->zeroSpinBox->setValue(nzero);
}

void WideGraph::setPalette(QString palette)
{
  ui->widePlot->setPalette(palette);
}
void WideGraph::setFsample(int n)
{
  m_fSample=n;
  ui->widePlot->setFsample(n);
}

void WideGraph::setMode65(int n)
{
  m_mode65=n;
  ui->widePlot->setMode65(n);
}

void WideGraph::on_cbFcenter_stateChanged(int n)
{
  m_bForceCenterFreq = (n!=0);
  if(m_bForceCenterFreq) {
    ui->fCenterLineEdit->setEnabled(true);
    ui->pbSetRxHardware->setEnabled(true);
  } else {
    ui->fCenterLineEdit->setDisabled(true);
    ui->pbSetRxHardware->setDisabled(true);
  }
}

void WideGraph::on_fCenterLineEdit_editingFinished()
{
  m_dForceCenterFreq=ui->fCenterLineEdit->text().toDouble();
}

void WideGraph::on_pbSetRxHardware_clicked()
{
  int iret=set570(m_mult570*(1.0+0.000001*m_cal570)*m_dForceCenterFreq);
  if(iret != 0) {
    QMessageBox mb;
    if(iret==-1) mb.setText("Failed to open Si570.");
    if(iret==-2) mb.setText("Frequency out of permitted range.");
    mb.exec();
  }
}

void WideGraph::initIQplus()
{
  int iret=set570(288.0);
  if(iret != 0) {
    QMessageBox mb;
    if(iret==-1) mb.setText("Failed to open Si570.");
    if(iret==-2) mb.setText("Frequency out of permitted range.");
    mb.exec();
  } else {
    on_pbSetRxHardware_clicked();
  }
}

void WideGraph::on_cbSpec2d_toggled(bool b)
{
  ui->widePlot->set2Dspec(b);
}

double WideGraph::fGreen()
{
  return ui->widePlot->fGreen();
}

void WideGraph::setPeriod(int n)
{
  m_TRperiod=n;
}

void WideGraph::on_cbLockTxRx_stateChanged(int n)
{
  m_bLockTxRx = (n!=0);
  ui->widePlot->setLockTxRx(m_bLockTxRx);
}

void WideGraph::rx570()
{
  double f=m_mult570*(1.0+0.000001*m_cal570)*m_dForceCenterFreq;
  int iret=set570(f);
  if(iret != 0) {
    QMessageBox mb;
    if(iret==-1) mb.setText("Failed to open Si570.");
    if(iret==-2) mb.setText("Frequency out of permitted range.");
    mb.exec();
  }
}

void WideGraph::tx570()
{
  if(m_bForceCenterFreq) datcom_.fcenter=m_dForceCenterFreq;
  m_bIQxt=true;
  double f=ui->widePlot->txFreq();
//  double f1=m_mult570Tx*(1.0+0.000001*m_cal570) * f;
  double f1=m_mult570Tx*(1.0+0.000001*m_cal570) * (f - m_TxOffset);

  int iret=set570(f1);
  if(iret != 0) {
    QMessageBox mb;
    if(iret==-1) mb.setText("Failed to open Si570.");
    if(iret==-2) mb.setText("Frequency out of permitted range.");
    mb.exec();
  }
}

void WideGraph::updateFreqLabel()
{
  auto rxFreq = QString {"%1"}.arg (ui->widePlot->rxFreq (), 10, 'f', 6);
  auto txFreq = QString {"%1"}.arg (ui->widePlot->txFreq (), 10, 'f', 6);
  rxFreq.insert (rxFreq.size () - 3, '.');
  txFreq.insert (txFreq.size () - 3, '.');
  ui->labFreq->setText (QString {"Rx:  %1\nTx:  %2"}.arg (rxFreq, txFreq));
}

void WideGraph::enableSetRxHardware(bool b)
{
  ui->pbSetRxHardware->setEnabled(b);
}
