// -*- Mode: C++ -*-
#ifndef JTTY_KEY_ROUTER_HPP_
#define JTTY_KEY_ROUTER_HPP_

#include <Qt>
#include <QString>

class QWidget;

namespace Jtty
{
  // Where keyboard focus sits, as far as JTTY key routing cares.
  enum class KeyFocus
  {
    TxBox,      // the Tx message box itself
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
      StepTxFrequency // Shift+F11 / Shift+F12; txStepHz holds the signed step
    };

    Action action {Action::None};
    QString text;
    int functionKey {0};
    int txStepHz {0};
  };

  inline constexpr int txFrequencyStepHz = 150;

  KeyFocus classifyKeyFocus (QWidget const * focus, QWidget const * txBox);

  KeyRoute routeKey (int key, Qt::KeyboardModifiers modifiers,
                     QString const& text, KeyFocus focus);
}

#endif
