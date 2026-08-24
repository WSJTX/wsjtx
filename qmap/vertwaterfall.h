#ifndef VERTWATERFALL_H
#define VERTWATERFALL_H

#include <QDialog>
#include <QList>
#include <QString>
#include "vplotter.h"

namespace Ui {
  class VertWaterfall;
}

// Optional alternative to the Wideband Waterfall (WideGraph): narrower and
// taller, frequency running bottom-to-top, spectra scrolling to the left,
// with a frequency scale and decoded-callsign labels at the right. Fed by
// WideGraph::spectrumReady so it shows exactly the same spectral data as
// the top of the Wideband Waterfall, not a second, independently-decimated
// copy of it.
class VertWaterfall : public QDialog
{
  Q_OBJECT

public:
  explicit VertWaterfall (QString const& settings_filename, QWidget * parent = nullptr);
  ~VertWaterfall();

  void saveSettings();

public slots:
  void dataSinkVert(const float swide[], int n, double startFreqKHz, double fSpanKHz,
                    int plotZero, int plotGain);
  // decode_secs: seconds-since-midnight-UTC parsed from the decode
  // line's own hhmmss (not wall-clock "now"). Also drives aging -- each
  // call sweeps out labels that have fallen kDecodeLabelLifetimeSecs
  // behind decode_secs, so stale entries clear whether monitoring live
  // or replaying a saved file at any speed.
  void addDecodeLabel(double freq_khz, QString const& callsign, bool second_half, int decode_secs);

signals:
  void decodeLabelClicked2(QString callsign, bool doubleClick);

protected:
  void closeEvent(QCloseEvent * event) override;

private slots:
  void on_cbShowCallsigns_toggled(bool checked);
  void vertDecodeLabelClicked(QString callsign, bool doubleClick);

private:
  void ageDecodeLabels(int nowSecs);

  Ui::VertWaterfall * ui;
  QString m_settings_filename;

  QList<VertDecodeLabel> m_decodeLabels;
  bool m_decodeLabelsEnabled;
  static constexpr int kDecodeLabelMax = 100;
  // Age out a callsign once its own decode timestamp falls this many
  // seconds behind the most recently arrived decode's timestamp.
  static constexpr int kDecodeLabelLifetimeSecs = 3*60;
};

#endif // VERTWATERFALL_H
