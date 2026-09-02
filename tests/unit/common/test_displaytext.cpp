#include <QApplication>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QToolButton>

#include "commons.h"
#include "widgets/displaytext.h"

namespace
{
  dec_data_t test_dec_data {};
  int constexpr tone_buffer_size {250};
}

// The transceiver objects linked through wsjt_qt reference globals normally owned by MainWindow.
dec_data_t& dec_data = test_dec_data;
int volatile itone[tone_buffer_size] {};
int volatile icw[tone_buffer_size] {};

float gran ()
{
  return 0.0f;
}

class TestDisplayText final
  : public QObject
{
  Q_OBJECT

private:
  static QPoint positionWithin (DisplayText const& display, QString const& text)
  {
    auto cursor = display.document ()->find (text);
    cursor.setPosition (cursor.selectionStart () + text.size () / 2);
    return display.cursorRect (cursor).center ();
  }

  static void sendMouseEvent (DisplayText& display, QEvent::Type type, QPoint const& position,
                              Qt::MouseButton button, Qt::MouseButtons buttons,
                              Qt::KeyboardModifiers modifiers = Qt::NoModifier)
  {
    QMouseEvent event {type, position, button, buttons, modifiers};
    QApplication::sendEvent (display.viewport (), &event);
  }

  static void sendClick (DisplayText& display, QPoint const& position)
  {
    sendMouseEvent (display, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
    sendMouseEvent (display, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
  }

  static void sendDoubleClick (DisplayText& display, QPoint const& position,
                               Qt::KeyboardModifiers modifiers = Qt::NoModifier)
  {
    sendMouseEvent (display, QEvent::MouseButtonDblClick, position, Qt::LeftButton,
                    Qt::LeftButton, modifiers);
    sendMouseEvent (display, QEvent::MouseButtonRelease, position, Qt::LeftButton,
                    Qt::NoButton, modifiers);
  }

  static void populateForScrolling (DisplayText& display, int count = 80)
  {
    for (int i = 0; i < count; ++i)
      {
        display.insertText (QString {"%1 decode"}.arg (i, 3, 10, QLatin1Char {'0'}));
      }
    QApplication::processEvents ();
  }

  static QString topVisibleLine (DisplayText const& display)
  {
    return display.cursorForPosition (QPoint {1, 1}).block ().text ();
  }

private slots:
  void insertionDoesNotRetargetDoubleClick ()
  {
    DisplayText display;
    display.resize (640, 240);
    display.insertText ("K1ABC intended decode");
    display.show ();
    QApplication::processEvents ();

    auto const position = positionWithin (display, "K1ABC");
    QCOMPARE (display.cursorForPosition (position).block ().text (), QString {"K1ABC intended decode"});

    QString selected_line;
    QString selected_word;
    Qt::KeyboardModifiers selected_modifiers;
    int selection_count {0};
    connect (&display, &DisplayText::selectCallsign,
             [&] (QString const& line, QString const& word, Qt::KeyboardModifiers modifiers) {
               selected_line = line;
               selected_word = word;
               selected_modifiers = modifiers;
               ++selection_count;
             });

    sendClick (display, position);
    display.insertText ("K9XYZ newest decode");

    QCOMPARE (display.textCursor ().block ().text (), QString {"K9XYZ newest decode"});
    QCOMPARE (display.cursorForPosition (position).block ().text (), QString {"K1ABC intended decode"});

    sendDoubleClick (display, position, Qt::ControlModifier);

    QCOMPARE (selection_count, 1);
    QCOMPARE (selected_line, QString {"K1ABC intended decode"});
    QCOMPARE (selected_word, QString {"K1ABC"});
    QCOMPARE (selected_modifiers, Qt::KeyboardModifiers {Qt::ControlModifier});
  }

  void movedContentKeepsFirstPressedTarget ()
  {
    DisplayText display;
    display.resize (640, 240);
    display.insertText ("K1ABC intended decode");
    display.show ();
    QApplication::processEvents ();

    auto const position = positionWithin (display, "K1ABC");
    QString selected_line;
    QString selected_word;
    connect (&display, &DisplayText::selectCallsign,
             [&] (QString const& line, QString const& word, Qt::KeyboardModifiers) {
               selected_line = line;
               selected_word = word;
             });

    sendClick (display, position);
    QTextCursor insertion {display.document ()};
    insertion.insertText ("K9XYZ inserted above\n");
    QApplication::processEvents ();

    QCOMPARE (display.cursorForPosition (position).block ().text (), QString {"K9XYZ inserted above"});

    sendDoubleClick (display, position);

    QCOMPARE (selected_line, QString {"K1ABC intended decode"});
    QCOMPARE (selected_word, QString {"K1ABC"});
  }

  void doubleClickWithoutPressUsesEventPosition ()
  {
    DisplayText display;
    display.resize (640, 240);
    display.insertText ("N0CALL direct target");
    display.show ();
    QApplication::processEvents ();

    auto const position = positionWithin (display, "N0CALL");
    QString selected_line;
    QString selected_word;
    connect (&display, &DisplayText::selectCallsign,
             [&] (QString const& line, QString const& word, Qt::KeyboardModifiers) {
               selected_line = line;
               selected_word = word;
             });

    sendDoubleClick (display, position);

    QCOMPARE (selected_line, QString {"N0CALL direct target"});
    QCOMPARE (selected_word, QString {"N0CALL"});
  }

  void eraseCancelsCapturedTarget ()
  {
    DisplayText display;
    display.resize (640, 240);
    display.insertText ("K1ABC stale target");
    display.show ();
    QApplication::processEvents ();

    auto const stale_position = positionWithin (display, "K1ABC");
    int selection_count {0};
    connect (&display, &DisplayText::selectCallsign,
             [&] (QString const&, QString const&, Qt::KeyboardModifiers) {
               ++selection_count;
             });

    sendClick (display, stale_position);
    display.erase ();
    display.insertText ("K9XYZ replacement target");
    QApplication::processEvents ();

    sendDoubleClick (display, positionWithin (display, "K9XYZ"));

    QCOMPARE (selection_count, 0);
  }

  void canceledDoubleClickRestoresDirectFallback ()
  {
    DisplayText display;
    display.resize (640, 240);
    display.insertText ("K1ABC stale target");
    display.show ();
    QApplication::processEvents ();

    auto const stale_position = positionWithin (display, "K1ABC");
    QString selected_line;
    QString selected_word;
    int selection_count {0};
    connect (&display, &DisplayText::selectCallsign,
             [&] (QString const& line, QString const& word, Qt::KeyboardModifiers) {
               selected_line = line;
               selected_word = word;
               ++selection_count;
             });

    sendClick (display, stale_position);
    display.clear ();
    display.insertText ("K9XYZ replacement target");
    QApplication::processEvents ();
    auto const replacement_position = positionWithin (display, "K9XYZ");

    sendDoubleClick (display, replacement_position);
    QCOMPARE (selection_count, 0);

    sendDoubleClick (display, replacement_position);
    QCOMPARE (selection_count, 1);
    QCOMPARE (selected_line, QString {"K9XYZ replacement target"});
    QCOMPARE (selected_word, QString {"K9XYZ"});
  }

  void pressAfterClearCapturesReplacement ()
  {
    DisplayText display;
    display.resize (640, 240);
    display.insertText ("K1ABC stale target");
    display.show ();
    QApplication::processEvents ();

    auto const stale_position = positionWithin (display, "K1ABC");
    QString selected_line;
    QString selected_word;
    connect (&display, &DisplayText::selectCallsign,
             [&] (QString const& line, QString const& word, Qt::KeyboardModifiers) {
               selected_line = line;
               selected_word = word;
             });

    sendClick (display, stale_position);
    display.clear ();
    display.insertText ("K9XYZ replacement target");
    QApplication::processEvents ();
    auto const replacement_position = positionWithin (display, "K9XYZ");

    sendClick (display, replacement_position);
    sendDoubleClick (display, replacement_position);

    QCOMPARE (selected_line, QString {"K9XYZ replacement target"});
    QCOMPARE (selected_word, QString {"K9XYZ"});
  }

  void stickyScrollPreservesParkedContent ()
  {
    DisplayText display;
    display.set_configuration (nullptr);
    display.resize (360, 140);
    display.show ();
    populateForScrolling (display);

    auto * scroll_bar = display.verticalScrollBar ();
    auto * return_button = display.findChild<QToolButton *> ("returnToLiveActivityButton");
    QVERIFY (return_button);
    QCOMPARE (scroll_bar->value (), scroll_bar->maximum ());

    scroll_bar->triggerAction (QAbstractSlider::SliderPageStepSub);
    QApplication::processEvents ();
    QVERIFY (scroll_bar->value () < scroll_bar->maximum ());
    QVERIFY (return_button->isVisible ());
    auto const parked_line = topVisibleLine (display);

    display.insertText ("new live decode");
    QApplication::processEvents ();

    QCOMPARE (topVisibleLine (display), parked_line);
    QVERIFY (return_button->isVisible ());
  }

  void formerLivePositionDoesNotResumeFollowing ()
  {
    DisplayText display;
    display.set_configuration (nullptr);
    display.resize (360, 140);
    display.show ();
    populateForScrolling (display);

    auto * scroll_bar = display.verticalScrollBar ();
    auto * return_button = display.findChild<QToolButton *> ("returnToLiveActivityButton");
    QVERIFY (return_button);
    auto const former_live_position = scroll_bar->maximum ();

    scroll_bar->triggerAction (QAbstractSlider::SliderPageStepSub);
    display.insertText ("first newer decode");
    QApplication::processEvents ();
    QVERIFY (scroll_bar->maximum () > former_live_position);

    scroll_bar->setSliderPosition (former_live_position);
    QVERIFY (QMetaObject::invokeMethod (scroll_bar, "sliderMoved",
                                        Q_ARG (int, former_live_position)));
    QApplication::processEvents ();
    QCOMPARE (scroll_bar->value (), former_live_position);
    QVERIFY (return_button->isVisible ());
    auto const parked_line = topVisibleLine (display);

    display.insertText ("second newer decode");
    QApplication::processEvents ();

    QCOMPARE (topVisibleLine (display), parked_line);
    QVERIFY (scroll_bar->value () < scroll_bar->maximum ());
  }

  void returnControlIsAccessibleAndViewportBounded ()
  {
    DisplayText display;
    display.set_configuration (nullptr);
    display.resize (360, 140);
    display.show ();
    populateForScrolling (display);

    auto * scroll_bar = display.verticalScrollBar ();
    auto * return_button = display.findChild<QToolButton *> ("returnToLiveActivityButton");
    QVERIFY (return_button);
    scroll_bar->triggerAction (QAbstractSlider::SliderPageStepSub);
    QApplication::processEvents ();

    auto large_font = return_button->font ();
    large_font.setPointSize (30);
    return_button->setFont (large_font);
    display.resize (120, 80);
    QApplication::processEvents ();

    QVERIFY (return_button->isVisible ());
    QCOMPARE (return_button->focusPolicy (), Qt::StrongFocus);
    QVERIFY (return_button->styleSheet ().isEmpty ());
    QVERIFY (display.viewport ()->rect ().contains (return_button->geometry ()));

    QTest::keyClick (return_button, Qt::Key_Space);

    QCOMPARE (scroll_bar->value (), scroll_bar->maximum ());
    QVERIFY (!return_button->isVisible ());
  }

  void unconfiguredDisplayKeepsOriginalFollowBehavior ()
  {
    DisplayText display;
    display.resize (360, 140);
    display.show ();
    populateForScrolling (display);

    auto * scroll_bar = display.verticalScrollBar ();
    auto * return_button = display.findChild<QToolButton *> ("returnToLiveActivityButton");
    QVERIFY (return_button);
    scroll_bar->triggerAction (QAbstractSlider::SliderPageStepSub);
    QApplication::processEvents ();
    QVERIFY (scroll_bar->value () < scroll_bar->maximum ());

    display.insertText ("queue update");
    QApplication::processEvents ();

    QCOMPARE (display.textCursor ().block ().text (), QString {"queue update"});
    QVERIFY (display.viewport ()->rect ().intersects (display.cursorRect (display.textCursor ())));
    QVERIFY (!return_button->isVisible ());
  }

  void fontChangePreservesParkedContent ()
  {
    DisplayText display;
    display.set_configuration (nullptr);
    display.resize (360, 140);
    display.show ();
    populateForScrolling (display);

    auto * scroll_bar = display.verticalScrollBar ();
    auto * return_button = display.findChild<QToolButton *> ("returnToLiveActivityButton");
    QVERIFY (return_button);
    scroll_bar->triggerAction (QAbstractSlider::SliderPageStepSub);
    QApplication::processEvents ();
    auto const parked_line = topVisibleLine (display);

    auto larger_font = display.contentFont ();
    larger_font.setPointSize (larger_font.pointSize () + 2);
    display.setContentFont (larger_font);
    QApplication::processEvents ();

    QCOMPARE (topVisibleLine (display), parked_line);
    QVERIFY (return_button->isVisible ());
  }
};

QTEST_MAIN (TestDisplayText)

#include "test_displaytext.moc"
