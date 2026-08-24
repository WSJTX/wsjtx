#include "vertwaterfall.h"
#include <algorithm>
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

  connect (ui->vertPlot, SIGNAL (decodeLabelClicked (QString,bool)), this,
           SLOT (vertDecodeLabelClicked (QString,bool)));
}

void VertWaterfall::vertDecodeLabelClicked(QString callsign, bool doubleClick)
{
  emit decodeLabelClicked2(callsign, doubleClick);
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

void VertWaterfall::addDecodeLabel(double freq_khz, QString const& callsign, bool second_half,
                                   int decode_secs)
{
  if (!m_decodeLabelsEnabled) return;
  ageDecodeLabels(decode_secs);

  for (auto& lab : m_decodeLabels) {
    if (lab.callsign == callsign) {
      lab.freq_khz = freq_khz;
      lab.last_seen_secs = decode_secs;
      lab.second_half = second_half;
      ui->vertPlot->setDecodeLabels(m_decodeLabels);
      return;
    }
  }
  if (m_decodeLabels.size() >= kDecodeLabelMax) {
    m_decodeLabels.removeFirst();
  }
  m_decodeLabels.append(VertDecodeLabel{freq_khz, callsign, decode_secs, second_half});
  ui->vertPlot->setDecodeLabels(m_decodeLabels);
}

void VertWaterfall::ageDecodeLabels(int nowSecs)
{
  if (m_decodeLabels.isEmpty()) return;
  int before = m_decodeLabels.size();
  m_decodeLabels.erase(
      std::remove_if(m_decodeLabels.begin(), m_decodeLabels.end(),
                     [nowSecs](VertDecodeLabel const& l) {
                         int delta = nowSecs - l.last_seen_secs;
                         if (delta < -43200) delta += 86400;   // UTC midnight wrap
                         else if (delta > 43200) delta -= 86400;
                         return delta > kDecodeLabelLifetimeSecs;
                     }),
      m_decodeLabels.end());
  if (m_decodeLabels.size() != before) {
    ui->vertPlot->setDecodeLabels(m_decodeLabels);
  }
}

void VertWaterfall::on_cbShowCallsigns_toggled(bool checked)
{
  m_decodeLabelsEnabled = checked;
  if (!checked) {
    m_decodeLabels.clear();
    ui->vertPlot->setDecodeLabels(m_decodeLabels);
  }
}
