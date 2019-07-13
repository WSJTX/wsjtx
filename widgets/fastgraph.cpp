#include "fastgraph.h"

#include "commons.h"
#include <QSettings>
#include <QApplication>
#include <QKeyEvent>
#include "fastplot.h"
#include "SettingsGroup.hpp"

#include "ui_fastgraph.h"
#include "moc_fastgraph.cpp"

#define NSMAX2 1366

FastGraph::FastGraph(QSettings * settings, QWidget *parent) :
  QDialog {parent, Qt::Window | Qt::WindowTitleHint |
           Qt::WindowCloseButtonHint |
           Qt::WindowMinimizeButtonHint},
  m_settings {settings},
  m_ave {40},
  ui {new Ui::FastGraph}
{
  ui->setupUi(this);
  setWindowTitle (QApplication::applicationName () + " - " + tr ("Fast Graph"));
  installEventFilter(parent);                   //Installing the filter
  ui->fastPlot->setCursor(Qt::CrossCursor);

  //Restore user's settings
  SettingsGroup g {m_settings, "FastGraph"};
  restoreGeometry (m_settings->value ("geometry", saveGeometry ()).toByteArray ());
  ui->fastPlot->setPlotZero(m_settings->value("PlotZero", 0).toInt());
  ui->fastPlot->setPlotGain(m_settings->value("PlotGain", 0).toInt());
  ui->zeroSlider->setValue(ui->fastPlot->m_plotZero);
  ui->gainSlider->setValue(ui->fastPlot->m_plotGain);
  ui->fastPlot->setGreenZero(m_settings->value("GreenZero", 0).toInt());
  ui->greenZeroSlider->setValue(ui->fastPlot->m_greenZero);
//  ui->controls_widget->setVisible (!m_settings->value("HideControls", false).toBool ());
  ui->controls_widget->setVisible(true);
  connect (ui->fastPlot, &FPlotter::fastPick, this, &FastGraph::fastPick);
}

void FastGraph::closeEvent (QCloseEvent * e)
{
  saveSettings ();
  QDialog::closeEvent (e);
}

FastGraph::~FastGraph ()
{
}

void FastGraph::saveSettings()
{
//Save user's settings
  SettingsGroup g {m_settings, "FastGraph"};
  m_settings->setValue ("geometry", saveGeometry ());
  m_settings->setValue("PlotZero",ui->fastPlot->m_plotZero);
  m_settings->setValue("PlotGain",ui->fastPlot->m_plotGain);
  m_settings->setValue("GreenZero",ui->fastPlot->m_greenZero);
  m_settings->setValue("GreenGain",ui->fastPlot->m_greenGain);
//  m_settings->setValue ("HideControls", ui->controls_widget->isHidden ());
}

void FastGraph::plotSpec(bool diskData, int UTCdisk)
{
  ui->fastPlot->m_diskData=diskData;
  ui->fastPlot->m_UTCdisk=UTCdisk;
  ui->fastPlot->draw();
}

void FastGraph::on_gainSlider_valueChanged(int value)
{
  ui->fastPlot->setPlotGain(value);
  ui->fastPlot->draw();
}

void FastGraph::on_zeroSlider_valueChanged(int value)
{
  ui->fastPlot->setPlotZero(value);
  ui->fastPlot->draw();
}

void FastGraph::on_greenZeroSlider_valueChanged(int value)
{
  ui->fastPlot->setGreenZero(value);
  ui->fastPlot->draw();
}

void FastGraph::setTRPeriod(double p)
{
  m_TRperiod=p;
  ui->fastPlot->setTRperiod(m_TRperiod);
}

void FastGraph::on_pbAutoLevel_clicked()
{
  float sum=0.0;
  for(int i=0; i<=fast_jh; i++) {
    sum += fast_green[i];
  }
  m_ave=sum/fast_jh;
  ui->gainSlider->setValue(127-int(2.2*m_ave));
  ui->zeroSlider->setValue(int(m_ave)+20);
  ui->greenZeroSlider->setValue(160-int(3.3*m_ave));
}

void FastGraph::setMode(QString mode)                              //setMode
{
  ui->fastPlot->setMode(mode);
}

void FastGraph::keyPressEvent(QKeyEvent *e)
{
  switch(e->key())
  {
/*
  case Qt::Key_M:
    if(e->modifiers() & Qt::ControlModifier) {
      ui->controls_widget->setVisible (!ui->controls_widget->isVisible ());
    }
    break;
*/
  default:
    QDialog::keyPressEvent (e);
  }
}
