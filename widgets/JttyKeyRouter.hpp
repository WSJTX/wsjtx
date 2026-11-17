// -*- Mode: C++ -*-
#ifndef JTTY_KEY_ROUTER_HPP_
#define JTTY_KEY_ROUTER_HPP_

#include <Qt>
#include <QString>

#include <initializer_list>

class QWidget;

namespace Jtty
{
  // Where keyboard focus sits, as far as JTTY key routing cares.
  enum class KeyFocus
  {
    TxBox,      // the Tx message box itself
    QsoField,   // DX Call, Call or Received: keeps typing, but Enter can send
    TextEntry,  // any other field that accepts typing; it keeps its keys
    Other       // buttons, decode panes, the waterfall, no focus at all
  };

  struct KeyRoute
  {
    enum class Action
    {
      None,           // not ours: let the focused widget handle it
      AppendToTxBox,  // text holds what to append
      BackspaceTxBox, // delete the last unsent character of the Tx box
      Abort,          // Esc: stop mid-frame and clear the Tx box
      TogglePtt,      // F9
      FunctionKey,    // F1-F8 give 1..8, Shift+F1-F8 give 9..16, in functionKey
      FocusDxCall,    // F10
      StepTxFrequency, // Shift+F11 / Shift+F12; txStepHz holds the signed step
      EnterSendsMessage, // Enter, when Enter Sends Message is on
      RecallHistory,    // Ctrl+Up / Ctrl+Down; recallStep is -1 (older) or +1
      ZoomFont          // Ctrl+= / Ctrl+- / Ctrl+0; zoomStep is +1, -1 or 0 (reset)
    };

    Action action {Action::None};
    QString text;
    int functionKey {0};
    int txStepHz {0};
    int recallStep {0};
    int zoomStep {0};
  };

  inline constexpr int txFrequencyStepHz = 150;

  KeyFocus classifyKeyFocus (QWidget const * focus, QWidget const * txBox,
                             std::initializer_list<QWidget const *> qsoFields = {});

  KeyRoute routeKey (int key, Qt::KeyboardModifiers modifiers,
                     QString const& text, KeyFocus focus,
                     bool enterSendsMessage = false);
}

#endif
