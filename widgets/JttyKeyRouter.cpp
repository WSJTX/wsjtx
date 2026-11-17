#include "JttyKeyRouter.hpp"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>

namespace Jtty
{
  KeyFocus classifyKeyFocus (QWidget const * focus, QWidget const * txBox,
                             std::initializer_list<QWidget const *> qsoFields)
  {
    if (!focus) return KeyFocus::Other;
    if (focus == txBox) return KeyFocus::TxBox;
    for (auto const * field : qsoFields) {
      if (focus == field) return KeyFocus::QsoField;
    }
    // A spin box's or editable combo's focus may sit on its embedded line edit.
    if (auto const * edit = qobject_cast<QLineEdit const *> (focus)) {
      return edit->isReadOnly () ? KeyFocus::Other : KeyFocus::TextEntry;
    }
    if (auto const * spin = qobject_cast<QAbstractSpinBox const *> (focus)) {
      return spin->isReadOnly () ? KeyFocus::Other : KeyFocus::TextEntry;
    }
    if (auto const * combo = qobject_cast<QComboBox const *> (focus)) {
      return combo->isEditable () && combo->lineEdit () && !combo->lineEdit ()->isReadOnly ()
        ? KeyFocus::TextEntry : KeyFocus::Other;
    }
    if (auto const * plain = qobject_cast<QPlainTextEdit const *> (focus)) {
      return plain->isReadOnly () ? KeyFocus::Other : KeyFocus::TextEntry;
    }
    if (auto const * rich = qobject_cast<QTextEdit const *> (focus)) {
      return rich->isReadOnly () ? KeyFocus::Other : KeyFocus::TextEntry;
    }
    return KeyFocus::Other;
  }

  namespace
  {
    bool isTypingText (QString const& text, Qt::KeyboardModifiers modifiers)
    {
      if (text.isEmpty ()) return false;
      for (QChar const c : text) {
        if (!c.isPrint ()) return false;
      }
      // Ctrl+Alt together is AltGr on Windows, which types ordinary characters.
      bool const ctrl = modifiers & Qt::ControlModifier;
      bool const alt = modifiers & Qt::AltModifier;
      if (modifiers & Qt::MetaModifier) return false;
      return ctrl == alt;
    }
  }

  KeyRoute routeKey (int key, Qt::KeyboardModifiers modifiers,
                     QString const& text, KeyFocus focus,
                     bool enterSendsMessage)
  {
    using Action = KeyRoute::Action;
    auto const mods = modifiers & ~Qt::KeypadModifier;

    if (key == Qt::Key_Escape && mods == Qt::NoModifier) return {Action::Abort, {}, 0};
    if (key == Qt::Key_F9 && mods == Qt::NoModifier) return {Action::TogglePtt, {}, 0};
    if (key == Qt::Key_F10 && mods == Qt::NoModifier) return {Action::FocusDxCall, {}, 0};
    if (key >= Qt::Key_F1 && key <= Qt::Key_F8 && mods == Qt::NoModifier) {
      return {Action::FunctionKey, {}, key - Qt::Key_F1 + 1};
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F8 && mods == Qt::ShiftModifier) {
      return {Action::FunctionKey, {}, key - Qt::Key_F1 + 9};
    }
    if ((key == Qt::Key_F11 || key == Qt::Key_F12) && mods == Qt::ShiftModifier) {
      return {Action::StepTxFrequency, {}, 0,
              key == Qt::Key_F11 ? -txFrequencyStepHz : txFrequencyStepHz};
    }

    // Ctrl+Shift+= types Key_Plus on most layouts, so take either.
    bool const zoomIn = (key == Qt::Key_Equal || key == Qt::Key_Plus)
      && (mods == Qt::ControlModifier || mods == (Qt::ControlModifier | Qt::ShiftModifier));
    if (zoomIn || ((key == Qt::Key_Minus || key == Qt::Key_0) && mods == Qt::ControlModifier)) {
      KeyRoute route {Action::ZoomFont, {}, 0};
      route.zoomStep = zoomIn ? 1 : key == Qt::Key_Minus ? -1 : 0;
      return route;
    }

    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && mods == Qt::NoModifier
        && enterSendsMessage && focus != KeyFocus::TextEntry) {
      return {Action::EnterSendsMessage, {}, 0};
    }

    if ((key == Qt::Key_Up || key == Qt::Key_Down) && mods == Qt::ControlModifier
        && (focus == KeyFocus::TxBox || focus == KeyFocus::Other)) {
      KeyRoute route {Action::RecallHistory, {}, 0};
      route.recallStep = key == Qt::Key_Up ? -1 : 1;
      return route;
    }

    if (focus != KeyFocus::Other) return {};

    if (key == Qt::Key_Backspace && mods == Qt::NoModifier) return {Action::BackspaceTxBox, {}, 0};
    if (isTypingText (text, modifiers)) return {Action::AppendToTxBox, text, 0};
    return {};
  }
}
