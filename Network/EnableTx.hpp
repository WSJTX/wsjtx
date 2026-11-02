#ifndef ENABLE_TX_HPP__
#define ENABLE_TX_HPP__

namespace EnableTx
{
  template <typename Button, typename ResetWatchdog>
  void execute (Button& button, ResetWatchdog reset_watchdog)
  {
    if (!button.isChecked ())
      {
        reset_watchdog ();
        button.click ();
      }
  }
}

#endif
