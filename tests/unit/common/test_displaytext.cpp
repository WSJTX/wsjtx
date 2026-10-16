#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QMouseEvent>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTextBlock>
#include <QTextCursor>
#include <QToolButton>

#include <memory>

#include "commons.h"
#include "Configuration.hpp"
#include "MetaDataRegistry.hpp"
#include "JttyReceiveLine.hpp"
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

  QTemporaryDir directory_;
  QNetworkAccessManager network_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Configuration> configuration_;

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

  static QString topVisibleDecode (DisplayText const& display)
  {
    return display.cursorForPosition (QPoint {1, 6}).block ().text ();
  }

  static void addPeriod (DisplayText& display, QString const& period, int count)
  {
    display.new_period ();
    for (int i = 0; i < count; ++i)
      {
        display.insertText (QString {"%1 decode %2"}.arg (period).arg (i, 2, 10, QLatin1Char {'0'}));
      }
    QApplication::processEvents ();
  }

  void createConfiguration (bool decodes_from_top)
  {
    configuration_.reset ();
    settings_.reset (new QSettings {directory_.filePath ("settings.ini"), QSettings::IniFormat});
    settings_->clear ();
    settings_->setValue ("Configuration/DecodesFromTop", decodes_from_top);
    settings_->sync ();
    configuration_.reset (new Configuration {&network_, QDir {directory_.path ()}, settings_.get (), nullptr});
  }

  bool enableDecodesFromTopInSettings ()
  {
    QDialog * dialog {};
    for (auto * widget : QApplication::topLevelWidgets ())
      {
        auto * candidate = qobject_cast<QDialog *> (widget);
        if (candidate && candidate->findChild<QCheckBox *> ("decodes_from_top_check_box"))
          {
            dialog = candidate;
            break;
          }
      }
    if (!dialog) return false;

    auto * checkbox = dialog->findChild<QCheckBox *> ("decodes_from_top_check_box");
    checkbox->setChecked (true);
    auto ensure_option = [dialog] (char const * object_name, char const * option) {
      if (auto * combo = dialog->findChild<QComboBox *> (object_name);
          combo && combo->currentIndex () < 0)
        {
          combo->addItem (option);
        }
    };
    ensure_option ("sound_input_combo_box", "Test input");
    ensure_option ("sound_input_channel_combo_box", "Mono");
    ensure_option ("sound_output_combo_box", "Test output");

    QTimer dismiss_message;
    dismiss_message.setSingleShot (true);
    connect (&dismiss_message, &QTimer::timeout, [] {
      for (auto * widget : QApplication::topLevelWidgets ())
        {
          if (auto * message = qobject_cast<QMessageBox *> (widget))
            {
              message->accept ();
            }
        }
    });
    dismiss_message.start (1000);
    auto const invoked = QMetaObject::invokeMethod (dialog, "accept", Qt::DirectConnection);
    dismiss_message.stop ();
    return invoked && configuration_->decodes_from_top ();
  }

private slots:
  void initTestCase ()
  {
    register_types ();
    QStandardPaths::setTestModeEnabled (true);
    QCoreApplication::setApplicationName ("test_displaytext");
    QVERIFY (directory_.isValid ());
  }

  void jttyTransmitHighlightDoesNotLeakIntoNewLines_data ()
  {
    QTest::addColumn<QString> ("message");
    QTest::newRow ("single line") << QString {"CQ TEST"};
    QTest::newRow ("multiple lines") << QString {"CQ TEST\nSECOND LINE"};
    QTest::newRow ("wrapped line") << QString (90, QLatin1Char {'A'});
  }

  void jttyTransmitHighlightDoesNotLeakIntoNewLines ()
  {
    QFETCH (QString, message);
    DisplayText display;
    JttyReceiveLine sent;
    JttyReceiveLine::Presentation const presentation {
      0, 1, QDateTime {}, 0.0, 1500, message, -10, true, Qt::yellow, Qt::black};
    auto const cursor = sent.render (*display.document (), presentation, {}, display.contentFont ());
    QVERIFY (!cursor.isNull ());
    display.setTextCursor (cursor);
    auto const transmitted = display.document ()->toPlainText ();

    display.insertText ("FT8 received message", Qt::green);
    auto received = display.document ()->lastBlock ();
    QCOMPARE (received.blockFormat ().background (), QBrush {Qt::green});
    QCOMPARE (received.charFormat ().background ().style (), Qt::NoBrush);
    for (auto fragment = received.begin (); !fragment.atEnd (); ++fragment)
      {
        QCOMPARE (fragment.fragment ().charFormat ().background ().style (), Qt::NoBrush);
      }

    display.insertText ("Another FT8 received message");
    received = display.document ()->lastBlock ();
    QCOMPARE (received.blockFormat ().background ().style (), Qt::NoBrush);
    QCOMPARE (received.charFormat ().background ().style (), Qt::NoBrush);
    QVERIFY (display.toPlainText ().startsWith (transmitted + '\n'));
    QTextCursor sent_text {display.document ()};
    sent_text.movePosition (QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    QCOMPARE (sent_text.charFormat ().background (), QBrush {Qt::yellow});
  }

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

  void decodesFromTopKeepsFollowingAcrossPeriods ()
  {
    createConfiguration (true);
    QVERIFY (configuration_->decodes_from_top ());

    DisplayText display;
    display.set_configuration (configuration_.get (), true);
    display.resize (360, 140);
    display.show ();
    auto * return_button = display.findChild<QToolButton *> ("returnToLiveActivityButton");
    QVERIFY (return_button);

    addPeriod (display, "P0", 30);
    addPeriod (display, "P1", 30);
    addPeriod (display, "P2", 30);
    QCOMPARE (topVisibleDecode (display), QString {"P2 decode 00"});

    auto * scroll_bar = display.verticalScrollBar ();
    auto const live_position = scroll_bar->value ();
    scroll_bar->triggerAction (QAbstractSlider::SliderSingleStepAdd);
    QApplication::processEvents ();

    QVERIFY (scroll_bar->value () > live_position);
    QVERIFY (topVisibleDecode (display).startsWith ("P2"));
    QVERIFY (!return_button->isVisible ());

    addPeriod (display, "P3", 10);

    QCOMPARE (topVisibleDecode (display), QString {"P3 decode 00"});
    QVERIFY (!return_button->isVisible ());
  }

  void rxFrequencyKeepsStickyScrollingWithDecodesFromTopSetting ()
  {
    createConfiguration (true);

    DisplayText display;
    display.set_configuration (configuration_.get ());
    display.resize (360, 140);
    display.show ();
    populateForScrolling (display);

    auto * scroll_bar = display.verticalScrollBar ();
    auto * return_button = display.findChild<QToolButton *> ("returnToLiveActivityButton");
    QVERIFY (return_button);
    scroll_bar->triggerAction (QAbstractSlider::SliderPageStepSub);
    QApplication::processEvents ();
    QVERIFY (return_button->isVisible ());
    auto const parked_line = topVisibleLine (display);

    display.insertText ("new live decode");
    QApplication::processEvents ();

    QCOMPARE (topVisibleLine (display), parked_line);
    QVERIFY (return_button->isVisible ());
  }

  void enablingDecodesFromTopResumesFollowing ()
  {
    createConfiguration (false);

    DisplayText display;
    display.set_configuration (configuration_.get (), true);
    display.resize (360, 140);
    display.show ();
    auto * return_button = display.findChild<QToolButton *> ("returnToLiveActivityButton");
    QVERIFY (return_button);

    addPeriod (display, "P0", 30);
    addPeriod (display, "P1", 30);

    auto * scroll_bar = display.verticalScrollBar ();
    scroll_bar->triggerAction (QAbstractSlider::SliderPageStepSub);
    QApplication::processEvents ();
    QVERIFY (return_button->isVisible ());

    QVERIFY (enableDecodesFromTopInSettings ());

    QVERIFY (!return_button->isVisible ());
    addPeriod (display, "P2", 10);
    QCOMPARE (topVisibleDecode (display), QString {"P2 decode 00"});
  }
};

QTEST_MAIN (TestDisplayText)

#include "test_displaytext.moc"
