///////////////////////////////////////////////////////////////////////////
// Rotated counterpart of plotter.h/CPlotter: frequency runs bottom-to-top
// instead of left-to-right, and spectra scroll to the left instead of
// scrolling down. Used by VertWaterfall, the optional narrow/tall sibling
// of the Horizontal Waterfall (WideGraph/CPlotter).
///////////////////////////////////////////////////////////////////////////

#ifndef VPLOTTER_H
#define VPLOTTER_H

#include <QFrame>
#include <QPixmap>
#include <QList>
#include <QVector>
#include <QString>
#include <QtGlobal>
#include <QRect>
#include <QFont>

#include "decode_click_coalescer.h"
#include "decode_label.h"

class QMouseEvent;
class QFontMetrics;

class CVertPlotter : public QFrame
{
  Q_OBJECT
public:
  explicit CVertPlotter(QWidget *parent = nullptr);
  ~CVertPlotter();

  QSize minimumSizeHint() const override;
  QSize sizeHint() const override;

  // swide[0..n-1] is the same decimated spectrum WideGraph paints as the
  // top row of its waterfall (increasing index = increasing frequency),
  // spanning [startFreqKHz, startFreqKHz+fSpanKHz]. plotZero/plotGain are
  // WideGraph's current values, so color mapping matches it exactly.
  void draw(const float swide[], int n, double startFreqKHz, double fSpanKHz,
            int plotZero, int plotGain);
  void setPalette(QString palette);
  void setDecodeLabels(const QList<QMapDecodeLabel>& labels);

signals:
  void decodeLabelClicked(QByteArray decodeRow, DecodeClickGesture gesture);

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
  void drawScale();
  int  yFromFreq(double freq_khz) const;
  void makeFrequencyStrs();

  // One remembered column (raw, pre-color-map), so a resize can repaint
  // the spectrogram from history instead of blanking it. Keeps its own
  // freq/gain context, so an old column still renders how it looked when
  // captured even if the live settings have since changed.
  struct HistoryLine {
    QVector<float> swide;
    double startFreqKHz;
    double fSpanKHz;
    int    plotZero;
    int    plotGain;
  };
  void rebuildFromHistory();
  QList<HistoryLine> m_history;   // front = newest
  static constexpr int kMaxHistory = 4096;

  // Painting and hit-testing share the same frequency and displaced text bounds.
  struct LabelLayout {
    QMapDecodeLabel label;
    int     trueY;
    int     dispY;
    int     textX;
    QRect   rect;
  };
  QVector<LabelLayout> computeLayout(QFontMetrics const& fm, int minSpacing) const;
  QFont labelFont() const;
  bool  hitTestDecodeLabel(QPoint const& pos, QMapDecodeLabel& label);

  QColor  m_ColorTbl[256];
  QPixmap m_waterfallPixmap;   // spectrogram: x=time (newest at right), y=frequency (high at top)
  QPixmap m_scalePixmap;       // frequency ticks/labels, drawn just right of the spectrogram
  QSize   m_size;
  int     m_scaleWidth;        // px reserved for the tick/label strip
  int     m_labelWidth;        // px reserved for decoded-callsign text, right of the scale
  double  m_startFreqKHz;      // frequency at the bottom edge
  double  m_fSpanKHz;
  QString m_HDivText[64];
  int     m_hdivs;
  double  m_freqPerDiv;
  double  m_freq0;             // frequency of the first (bottom-most) tick

  QList<QMapDecodeLabel> m_decodeLabels;
  DecodeClickCoalescer m_decodeClickCoalescer;
};

#endif // VPLOTTER_H
