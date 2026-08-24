#include "widegraph.h"
#include <algorithm>
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
  setWindowTitle("Wideband Waterfall");
  setWindowFlags(Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint);
  installEventFilter(parent); //Installing the filter
  ui->widePlot->setCursor(Qt::CrossCursor);
  setMaximumWidth(2048);
  setMaximumHeight(880);
  ui->widePlot->setMaximumHeight(800);
  connect(ui->widePlot, SIGNAL(freezeDecode1(int)),this,
          SLOT(wideFreezeDecode(int)));
  connect(ui->widePlot, SIGNAL(decodeLabelClicked(QString,bool)),this,
          SLOT(wideDecodeLabelClicked(QString,bool)));

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
    for (int i=0; i<NFFT; i++) {
      splot[i] /= n;                       //Normalize the average
    }
    n=0;

    // Not yet sized (window never shown): plotWidth()==0 makes the loop
    // below index splot[] and lstrong[] far out of bounds, independent of
    // draw()'s own guard against this same never-shown case.
    if (ui->widePlot->plotWidth() <= 0) return;

    int w=ui->widePlot->plotWidth();
    qint64 sf = nkhz - 0.5*w*nbpp*df/1000.0;
    if(sf != ui->widePlot->startFreq()) ui->widePlot->SetStartFreq(sf);
    int i0=16384.0+(ui->widePlot->startFreq()-nkhz+0.001*m_fCal) * 1000.0/df + 0.5;
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
    emit spectrumReady(swide, qMin(w,2048), ui->widePlot->startFreq(), ui->widePlot->m_fSpan,
                       ui->widePlot->getPlotZero(), ui->widePlot->getPlotGain());
  }
}

void WideGraph::addDecodeLabel(double freq_khz, QString const& callsign,
                               bool second_half, int decode_secs)
{
  ageDecodeLabels(decode_secs);

  for (auto& lab : m_decodeLabels) {
    if (lab.callsign == callsign) {
      lab.freq_khz = freq_khz;
      lab.last_seen_secs = decode_secs;
      lab.second_half = second_half;
      if (ui && ui->widePlot) ui->widePlot->setDecodeLabels(m_decodeLabels);
      return;
    }
  }
  if (m_decodeLabels.size() >= kDecodeLabelMax) {
    m_decodeLabels.removeFirst();
  }
  m_decodeLabels.append(WideDecodeLabel{freq_khz, callsign, decode_secs, second_half});
  if (ui && ui->widePlot) ui->widePlot->setDecodeLabels(m_decodeLabels);
}

void WideGraph::ageDecodeLabels(int nowSecs)
{
  if (m_decodeLabels.isEmpty()) return;
  int before = m_decodeLabels.size();
  m_decodeLabels.erase(
      std::remove_if(m_decodeLabels.begin(), m_decodeLabels.end(),
                     [nowSecs](WideDecodeLabel const& l) {
                         int delta = nowSecs - l.last_seen_secs;
                         if (delta < -43200) delta += 86400;   // UTC midnight wrap
                         else if (delta > 43200) delta -= 86400;
                         return delta > kDecodeLabelLifetimeSecs;
                     }),
      m_decodeLabels.end());
  if (m_decodeLabels.size() != before && ui && ui->widePlot) {
    ui->widePlot->setDecodeLabels(m_decodeLabels);
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

void WideGraph::wideDecodeLabelClicked(QString callsign, bool doubleClick)
{
  emit decodeLabelClicked2(callsign, doubleClick);
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

void WideGraph::updateFreqLabel()
{
  auto rxFreq = QString {"%1"}.arg (ui->widePlot->rxFreq (), 10, 'f', 6);
  rxFreq.insert (rxFreq.size () - 3, '.');
  ui->labFreq->setText (QString {"Center freq:  %1"}.arg (rxFreq));
}
