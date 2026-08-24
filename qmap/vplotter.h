///////////////////////////////////////////////////////////////////////////
// Rotated counterpart of plotter.h/CPlotter: frequency runs bottom-to-top
// instead of left-to-right, and spectra scroll to the left instead of
// scrolling down. Used by VertWaterfall, the optional narrow/tall sibling
// of the Wideband Waterfall (WideGraph/CPlotter).
///////////////////////////////////////////////////////////////////////////

#ifndef VPLOTTER_H
#define VPLOTTER_H

#include <QFrame>
#include <QPixmap>
#include <QList>
#include <QVector>
#include <QString>
#include <QtGlobal>

// One decoded-callsign label drawn in the label strip, at the y-position
// matching its frequency. Mirrors the *design* of map65/decode_label.h's
// DecodeLabel (separate executable, so not shared code).
struct VertDecodeLabel
{
  double  freq_khz;
  QString callsign;
  // Seconds-since-midnight-UTC of the decode line's own hhmmss, not
  // wall-clock time -- so aging tracks the decoded data's own timeline,
  // correct whether monitoring live or replaying a saved .iq/.qm file
  // at any speed.
  int     last_seen_secs;
  // true if this decode is from the second 30 s half of a 60 s Rx
  // interval (e.g. a 30-second submode decoded alongside a 60-second
  // one at the same tone spacing). Always indented right a bit, so
  // its recency is visible even when nothing else is nearby.
  bool    second_half;
};

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
  void setDecodeLabels(const QList<VertDecodeLabel>& labels);

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

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

  // One label after vertical de-collision: trueY is where its dot sits
  // (the real frequency position); dispY is where its text is drawn,
  // pushed down just enough to keep a minimum line spacing from the
  // label above it. A leader line connects the two when they differ.
  struct LabelLayout {
    QString callsign;
    bool    second_half;
    int     trueY;
    int     dispY;
  };
  QVector<LabelLayout> computeLayout(int minSpacing) const;

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

  QList<VertDecodeLabel> m_decodeLabels;
};

#endif // VPLOTTER_H
