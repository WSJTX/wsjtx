// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef COMPACT_BUTTON_SIZE_HPP
#define COMPACT_BUTTON_SIZE_HPP

#include <QPushButton>
#include <QStyle>
#include <QStyleOptionButton>

// Measure the label and native decoration without the style's recommended
// dialog-button width (75 logical pixels on Windows). Painting stays native.
inline QSize compactButtonSize (QPushButton const * button)
{
  QStyleOptionButton option;
  option.initFrom (button);
  if (button->autoDefault ()) option.features |= QStyleOptionButton::AutoDefaultButton;
  auto contents = button->fontMetrics ().size (Qt::TextShowMnemonic, button->text ());
  // The text is already measured above. An empty option text avoids the
  // Windows style's dialog-width floor without discarding its frame margins.
  return button->style ()->sizeFromContents (QStyle::CT_PushButton, &option, contents, button);
}

#endif
