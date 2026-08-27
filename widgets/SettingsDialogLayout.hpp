#ifndef SETTINGS_DIALOG_LAYOUT_HPP_
#define SETTINGS_DIALOG_LAYOUT_HPP_

#include <QMargins>
#include <QSize>

class QScrollArea;
class QWidget;

namespace Ui
{
  class configuration_dialog;
}

namespace SettingsDialogLayout
{
  void install (Ui::configuration_dialog const& ui);
  QScrollArea * pageScrollArea (QWidget * page);
  QSize boundedWindowSize (QSize requested, QSize available, QMargins frame_margins);
}

#endif
