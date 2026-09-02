// -*- Mode: C++ -*-
#ifndef DISPLAYTEXT_H
#define DISPLAYTEXT_H

#include <QTextEdit>
#include <QFont>
#include <QHash>
#include <QPair>
#include <QString>
#include <QTimer>

class QAction;
class QResizeEvent;
class QToolButton;
class Configuration;
class LogBook;
class DecodedText;

class DisplayText
  : public QTextEdit
{
  Q_OBJECT
public:
  explicit DisplayText(QWidget *parent = nullptr);
  void set_configuration (Configuration const * configuration, bool high_volume = false);
  void setContentFont (QFont const&);
  QFont contentFont () const {return char_font_;}
  void insertLineSpacer(QString const&);
  bool displayDecodedText(DecodedText const& decodedText, QString const& myCall, QString const& mode,
                          bool displayDXCCEntity, LogBook const& logBook,
                          QString const& currentBand=QString {}, bool ppfx=false, bool bCQonly=false,
                          bool haveFSpread = false, float fSpread = 0.0, bool bDisplayPoints=false,
                          int points=-99, QString distance = "", bool alertsMuted=false);
  void displayTransmittedText(QString text, QString modeTx, qint32 txFreq, bool bFastMode,
                              double TRperiod, bool bSuperfox);
  void displayQSY(QString text);
  void displayHoundToBeCalled(QString t, bool bAtTop=false, QColor bg = QColor {}, QColor fg = QColor {});
  void setHighlightedHoundText(QString text);
  void new_period ();
  QString CQPriority(){return m_CQPriority;};
  qint32 m_points;
  bool m_bDisplayPoints;

  Q_SIGNAL void selectCallsign (QString const& line, QString const& word, Qt::KeyboardModifiers);
  Q_SIGNAL void erased ();

  Q_SLOT void insertText (QString const& text, QColor bg = QColor {}, QColor fg = QColor {}
                          , QString const& call1 = QString {}, QString const& call2 = QString {}, QTextCursor::MoveOperation location=QTextCursor::End);
  Q_SLOT void clear ();
  Q_SLOT void erase ();
  Q_SLOT void scrollToLiveActivity ();
  Q_SLOT void highlight_callsign (QString const& callsign, QColor const& bg, QColor const& fg, bool last_period_only);

private:
  void AudioAlerts();
  QTimer alertsTimer;
  QString leftJustifyAppendage (QString message, QString const& appendage) const;
  void captureClick (QMouseEvent const *);
  void mousePressEvent (QMouseEvent *) override;
  void mouseDoubleClickEvent (QMouseEvent *) override;
  void resizeEvent (QResizeEvent *) override;

  bool decodesFromTop () const;
  void extend_vertical_scrollbar (int min, int max);
  void userScrolledTo (int position);
  void updateReturnToLiveButton ();

  Configuration const * m_config;
  bool m_bPrincipalPrefix;
  QString m_CQPriority;
  QString appendWorkedB4(QString message, QString callsign
                         , QString const& grid, QColor * bg, QColor * fg
                         , LogBook const& logBook, QString const& currentBand
                         , QString const& currentMode, QString extra
                         , QString const& state = QString {}
                         , bool entityMismatch = false);
  QFont char_font_;
  QAction * erase_action_;
  enum class ClickState
  {
    None,
    Captured,
    Canceled,
  };
  QString pressed_line_;
  QString pressed_word_;
  Qt::MouseButton pressed_button_;
  ClickState click_state_;

  QHash<QString, QPair<QColor, QColor>> highlighted_calls_;
  bool high_volume_;
  bool sticky_scroll_enabled_;
  bool following_live_activity_;
  QMetaObject::Connection vertical_scroll_connection_;
  long long modified_vertical_scrollbar_max_;
  QToolButton * return_to_live_button_;
  int live_scroll_position_;
};

#endif // DISPLAYTEXT_H
