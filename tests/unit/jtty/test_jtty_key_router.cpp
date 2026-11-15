#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>

#include "widgets/JttyKeyRouter.hpp"

using Jtty::KeyFocus;
using Action = Jtty::KeyRoute::Action;

class TestJttyKeyRouter final
  : public QObject
{
  Q_OBJECT

  static Jtty::KeyRoute route (int key, Qt::KeyboardModifiers mods,
                               QString const& text, KeyFocus focus)
  {
    return Jtty::routeKey (key, mods, text, focus);
  }

private slots:

  void globalKeysWorkFromEveryFocus ()
  {
    for (auto focus : {KeyFocus::TxBox, KeyFocus::QsoField, KeyFocus::TextEntry, KeyFocus::Other}) {
      QCOMPARE (route (Qt::Key_Escape, Qt::NoModifier, QStringLiteral ("\x1b"), focus).action,
                Action::Abort);
      QCOMPARE (route (Qt::Key_F9, Qt::NoModifier, {}, focus).action, Action::TogglePtt);
      QCOMPARE (route (Qt::Key_F10, Qt::NoModifier, {}, focus).action, Action::FocusDxCall);
      for (int n = 1; n <= 8; ++n) {
        auto const r = route (Qt::Key_F1 + n - 1, Qt::NoModifier, {}, focus);
        QCOMPARE (r.action, Action::FunctionKey);
        QCOMPARE (r.functionKey, n);
      }
    }
  }

  void shiftF1ToF8AreTheSecondMacroBank ()
  {
    // In JTTY these override Shift+F1 (copyright notice) and Shift+F6
    // (decode remaining files).
    for (auto focus : {KeyFocus::TxBox, KeyFocus::QsoField, KeyFocus::TextEntry, KeyFocus::Other}) {
      for (int n = 1; n <= 8; ++n) {
        auto const r = route (Qt::Key_F1 + n - 1, Qt::ShiftModifier, {}, focus);
        QCOMPARE (r.action, Action::FunctionKey);
        QCOMPARE (r.functionKey, n + 8);
      }
    }
  }

  void otherModifiedFunctionKeysKeepTheirMeaning ()
  {
    QCOMPARE (route (Qt::Key_F1, Qt::ControlModifier, {}, KeyFocus::Other).action, Action::None);
    QCOMPARE (route (Qt::Key_F6, Qt::AltModifier, {}, KeyFocus::Other).action, Action::None);
    QCOMPARE (route (Qt::Key_F9, Qt::ControlModifier, {}, KeyFocus::Other).action, Action::None);
    QCOMPARE (route (Qt::Key_F9, Qt::ShiftModifier, {}, KeyFocus::Other).action, Action::None);
  }

  void rxFrequencyStepKeysPassThrough ()
  {
    QCOMPARE (route (Qt::Key_F11, Qt::NoModifier, {}, KeyFocus::Other).action, Action::None);
    QCOMPARE (route (Qt::Key_F12, Qt::NoModifier, {}, KeyFocus::Other).action, Action::None);
    QCOMPARE (route (Qt::Key_F11, Qt::ControlModifier, {}, KeyFocus::Other).action, Action::None);
    // Ctrl+Shift+F11/F12 is the existing 1 Hz dial-frequency step.
    QCOMPARE (route (Qt::Key_F11, Qt::ControlModifier | Qt::ShiftModifier, {}, KeyFocus::Other).action,
              Action::None);
  }

  void shiftF11F12StepTxFrequencyFromEveryFocus ()
  {
    for (auto focus : {KeyFocus::TxBox, KeyFocus::QsoField, KeyFocus::TextEntry, KeyFocus::Other}) {
      auto r = route (Qt::Key_F11, Qt::ShiftModifier, {}, focus);
      QCOMPARE (r.action, Action::StepTxFrequency);
      QCOMPARE (r.txStepHz, -150);
      r = route (Qt::Key_F12, Qt::ShiftModifier, {}, focus);
      QCOMPARE (r.action, Action::StepTxFrequency);
      QCOMPARE (r.txStepHz, 150);
    }
  }

  void typingAwayFromAnyFieldGoesToTheTxBox ()
  {
    auto r = route (Qt::Key_A, Qt::NoModifier, QStringLiteral ("a"), KeyFocus::Other);
    QCOMPARE (r.action, Action::AppendToTxBox);
    QCOMPARE (r.text, QStringLiteral ("a"));
    r = route (Qt::Key_Question, Qt::ShiftModifier, QStringLiteral ("?"), KeyFocus::Other);
    QCOMPARE (r.action, Action::AppendToTxBox);
    QCOMPARE (r.text, QStringLiteral ("?"));
    r = route (Qt::Key_Space, Qt::NoModifier, QStringLiteral (" "), KeyFocus::Other);
    QCOMPARE (r.action, Action::AppendToTxBox);
    r = route (Qt::Key_5, Qt::KeypadModifier, QStringLiteral ("5"), KeyFocus::Other);
    QCOMPARE (r.action, Action::AppendToTxBox);
  }

  void altGrCharactersAreTyping ()
  {
    auto const r = route (Qt::Key_At, Qt::ControlModifier | Qt::AltModifier,
                          QStringLiteral ("@"), KeyFocus::Other);
    QCOMPARE (r.action, Action::AppendToTxBox);
  }

  void shortcutsAreNotTyping ()
  {
    QCOMPARE (route (Qt::Key_C, Qt::ControlModifier, QStringLiteral ("\x03"), KeyFocus::Other).action,
              Action::None);
    QCOMPARE (route (Qt::Key_H, Qt::AltModifier, QStringLiteral ("h"), KeyFocus::Other).action,
              Action::None);
    QCOMPARE (route (Qt::Key_A, Qt::MetaModifier, QStringLiteral ("a"), KeyFocus::Other).action,
              Action::None);
  }

  void fieldsThatTakeTypingKeepTheirKeys ()
  {
    for (auto focus : {KeyFocus::TxBox, KeyFocus::QsoField, KeyFocus::TextEntry}) {
      QCOMPARE (route (Qt::Key_A, Qt::NoModifier, QStringLiteral ("a"), focus).action, Action::None);
      QCOMPARE (route (Qt::Key_Backspace, Qt::NoModifier, QStringLiteral ("\b"), focus).action,
                Action::None);
    }
  }

  void backspaceAwayFromAnyFieldEditsTheTxBox ()
  {
    QCOMPARE (route (Qt::Key_Backspace, Qt::NoModifier, QStringLiteral ("\b"), KeyFocus::Other).action,
              Action::BackspaceTxBox);
  }

  void navigationAndEnterAreLeftAlone ()
  {
    for (int key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down, Qt::Key_PageUp,
                    Qt::Key_PageDown, Qt::Key_Home, Qt::Key_End, Qt::Key_Tab}) {
      QCOMPARE (route (key, Qt::NoModifier, {}, KeyFocus::Other).action, Action::None);
    }
    QCOMPARE (route (Qt::Key_Return, Qt::NoModifier, QStringLiteral ("\r"), KeyFocus::Other).action,
              Action::None);
    QCOMPARE (route (Qt::Key_Tab, Qt::NoModifier, QStringLiteral ("\t"), KeyFocus::Other).action,
              Action::None);
  }

  void enterSendsMessageWhenOn ()
  {
    for (int key : {Qt::Key_Return, Qt::Key_Enter}) {
      for (auto focus : {KeyFocus::TxBox, KeyFocus::QsoField, KeyFocus::Other}) {
        QCOMPARE (Jtty::routeKey (key, Qt::NoModifier, QStringLiteral ("\r"), focus, true).action,
                  Action::EnterSendsMessage);
        QCOMPARE (Jtty::routeKey (key, Qt::NoModifier, QStringLiteral ("\r"), focus, false).action,
                  Action::None);
      }
      QCOMPARE (Jtty::routeKey (key, Qt::KeypadModifier, QStringLiteral ("\r"), KeyFocus::TxBox, true).action,
                Action::EnterSendsMessage);
      // Other fields keep Enter, and modified Enter is never ours.
      QCOMPARE (Jtty::routeKey (key, Qt::NoModifier, QStringLiteral ("\r"), KeyFocus::TextEntry, true).action,
                Action::None);
      QCOMPARE (Jtty::routeKey (key, Qt::ShiftModifier, QStringLiteral ("\r"), KeyFocus::TxBox, true).action,
                Action::None);
    }
  }

  void ctrlUpDownRecallFromTheTxBoxOrAway ()
  {
    for (auto focus : {KeyFocus::TxBox, KeyFocus::Other}) {
      auto r = route (Qt::Key_Up, Qt::ControlModifier, {}, focus);
      QCOMPARE (r.action, Action::RecallHistory);
      QCOMPARE (r.recallStep, -1);
      r = route (Qt::Key_Down, Qt::ControlModifier, {}, focus);
      QCOMPARE (r.action, Action::RecallHistory);
      QCOMPARE (r.recallStep, 1);
    }
    for (auto focus : {KeyFocus::QsoField, KeyFocus::TextEntry}) {
      QCOMPARE (route (Qt::Key_Up, Qt::ControlModifier, {}, focus).action, Action::None);
    }
    QCOMPARE (route (Qt::Key_Up, Qt::NoModifier, {}, KeyFocus::TxBox).action, Action::None);
  }

  void ctrlPlusMinusZeroZoomFromAnywhere ()
  {
    for (auto focus : {KeyFocus::TxBox, KeyFocus::QsoField, KeyFocus::TextEntry, KeyFocus::Other}) {
      auto r = route (Qt::Key_Equal, Qt::ControlModifier, {}, focus);
      QCOMPARE (r.action, Action::ZoomFont);
      QCOMPARE (r.zoomStep, 1);
      r = route (Qt::Key_Plus, Qt::ControlModifier | Qt::ShiftModifier, {}, focus);
      QCOMPARE (r.action, Action::ZoomFont);
      QCOMPARE (r.zoomStep, 1);
      r = route (Qt::Key_Plus, Qt::ControlModifier | Qt::KeypadModifier, {}, focus);
      QCOMPARE (r.zoomStep, 1);
      r = route (Qt::Key_Minus, Qt::ControlModifier, {}, focus);
      QCOMPARE (r.action, Action::ZoomFont);
      QCOMPARE (r.zoomStep, -1);
      r = route (Qt::Key_0, Qt::ControlModifier, {}, focus);
      QCOMPARE (r.action, Action::ZoomFont);
      QCOMPARE (r.zoomStep, 0);
    }
    // Unmodified they are ordinary typing.
    QCOMPARE (route (Qt::Key_Minus, Qt::NoModifier, QStringLiteral ("-"), KeyFocus::Other).action,
              Action::AppendToTxBox);
    QCOMPARE (route (Qt::Key_0, Qt::NoModifier, QStringLiteral ("0"), KeyFocus::TxBox).action,
              Action::None);
  }

  void classifiesFocusWidgets ()
  {
    QPlainTextEdit txBox;
    QCOMPARE (Jtty::classifyKeyFocus (&txBox, &txBox), KeyFocus::TxBox);
    QCOMPARE (Jtty::classifyKeyFocus (nullptr, &txBox), KeyFocus::Other);

    QLineEdit dxCall;
    QLineEdit received;
    QCOMPARE (Jtty::classifyKeyFocus (&received, &txBox, {&received, &dxCall}), KeyFocus::QsoField);
    QCOMPARE (Jtty::classifyKeyFocus (&dxCall, &txBox, {&received, &dxCall}), KeyFocus::QsoField);
    QCOMPARE (Jtty::classifyKeyFocus (&dxCall, &txBox), KeyFocus::TextEntry);

    QLineEdit edit;
    QCOMPARE (Jtty::classifyKeyFocus (&edit, &txBox), KeyFocus::TextEntry);
    edit.setReadOnly (true);
    QCOMPARE (Jtty::classifyKeyFocus (&edit, &txBox), KeyFocus::Other);

    QSpinBox spin;
    QCOMPARE (Jtty::classifyKeyFocus (&spin, &txBox), KeyFocus::TextEntry);

    QComboBox combo;
    QCOMPARE (Jtty::classifyKeyFocus (&combo, &txBox), KeyFocus::Other);
    combo.setEditable (true);
    QCOMPARE (Jtty::classifyKeyFocus (&combo, &txBox), KeyFocus::TextEntry);
    QCOMPARE (Jtty::classifyKeyFocus (combo.lineEdit (), &txBox), KeyFocus::TextEntry);
    // Editable only to centre its text, like the JTTY macro-set combo.
    combo.lineEdit ()->setReadOnly (true);
    QCOMPARE (Jtty::classifyKeyFocus (&combo, &txBox), KeyFocus::Other);
    QCOMPARE (Jtty::classifyKeyFocus (combo.lineEdit (), &txBox), KeyFocus::Other);

    QPlainTextEdit otherPlain;
    QCOMPARE (Jtty::classifyKeyFocus (&otherPlain, &txBox), KeyFocus::TextEntry);

    QTextEdit decodePane;
    decodePane.setReadOnly (true);
    QCOMPARE (Jtty::classifyKeyFocus (&decodePane, &txBox), KeyFocus::Other);

    QPushButton button;
    QCOMPARE (Jtty::classifyKeyFocus (&button, &txBox), KeyFocus::Other);
    QCheckBox check;
    QCOMPARE (Jtty::classifyKeyFocus (&check, &txBox), KeyFocus::Other);
  }
};

QTEST_MAIN (TestJttyKeyRouter)

#include "test_jtty_key_router.moc"
