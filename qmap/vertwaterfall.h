#ifndef VERTWATERFALL_H
#define VERTWATERFALL_H

#include <QDialog>
#include <QList>
#include <QString>
#include "vplotter.h"

namespace Ui {
  class VertWaterfall;
}

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
  void addDecodeLabel(QMapDecodeRecord const& record);

signals:
  void decodeLabelClicked2(QByteArray decodeRow, DecodeClickGesture gesture);

protected:
  void closeEvent(QCloseEvent * event) override;

private slots:
  void on_cbShowCallsigns_toggled(bool checked);

private:
  Ui::VertWaterfall * ui;
  QString m_settings_filename;

  QList<QMapDecodeLabel> m_decodeLabels;
  bool m_decodeLabelsEnabled;
};

#endif // VERTWATERFALL_H
