#include "vertwaterfall.h"
#include <QSettings>
#include "SettingsGroup.hpp"
#include "ui_vertwaterfall.h"

VertWaterfall::VertWaterfall (QString const& settings_filename, QWidget * parent)
  : QDialog {parent},
    ui {new Ui::VertWaterfall},
    m_settings_filename {settings_filename},
    m_decodeLabelsEnabled {true}
{
  ui->setupUi(this);
  setWindowTitle("Vertical Waterfall");
  setWindowFlags(Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint);

  QSettings settings {m_settings_filename, QSettings::IniFormat};
  {
    SettingsGroup g {&settings, "VertWaterfall"};
    setGeometry (settings.value ("VertWaterfallGeom", QRect {80,80,300,900}).toRect ());
    m_decodeLabelsEnabled = settings.value ("decode_labels_enabled", true).toBool ();
  }
  ui->cbShowCallsigns->setChecked (m_decodeLabelsEnabled);

  connect (ui->vertPlot, SIGNAL (decodeLabelClicked (QByteArray,bool)), this,
           SLOT (vertDecodeLabelClicked (QByteArray,bool)));
}

void VertWaterfall::vertDecodeLabelClicked(QByteArray decodeRow, bool doubleClick)
{
  emit decodeLabelClicked2(decodeRow, doubleClick);
}

VertWaterfall::~VertWaterfall()
{
  saveSettings();
  delete ui;
}

void VertWaterfall::saveSettings()
{
  QSettings settings {m_settings_filename, QSettings::IniFormat};
  SettingsGroup g {&settings, "VertWaterfall"};
  settings.setValue ("VertWaterfallGeom", geometry());
  settings.setValue ("decode_labels_enabled", m_decodeLabelsEnabled);
}

void VertWaterfall::closeEvent(QCloseEvent * event)
{
  saveSettings();
  QDialog::closeEvent(event);
}

void VertWaterfall::dataSinkVert(const float swide[], int n, double startFreqKHz, double fSpanKHz,
                                 int plotZero, int plotGain)
{
  ui->vertPlot->draw(swide, n, startFreqKHz, fSpanKHz, plotZero, plotGain);
}

void VertWaterfall::addDecodeLabel(QMapDecodeRecord const& record)
{
  if (!m_decodeLabelsEnabled) return;
  pruneQMapDecodeLabels(m_decodeLabels, record.secondsSinceMidnight);
  upsertQMapDecodeLabel(m_decodeLabels, record);
  ui->vertPlot->setDecodeLabels(m_decodeLabels);
}

void VertWaterfall::on_cbShowCallsigns_toggled(bool checked)
{
  m_decodeLabelsEnabled = checked;
  if (!checked) {
    m_decodeLabels.clear();
    ui->vertPlot->setDecodeLabels(m_decodeLabels);
  }
}
