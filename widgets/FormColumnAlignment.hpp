// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FORM_COLUMN_ALIGNMENT_HPP
#define FORM_COLUMN_ALIGNMENT_HPP

#include <initializer_list>

#include <QLabel>
#include <QWidget>

// One width for a column's labels and one for its controls, so the controls
// of its label + control rows line up.
inline void alignFormColumn (std::initializer_list<QLabel *> labels,
                             std::initializer_list<QWidget *> controls)
{
  int labelWidth = 0;
  for (auto * label : labels)
    {
      label->setMinimumWidth (0);
      label->ensurePolished ();
      labelWidth = qMax (labelWidth, label->sizeHint ().width ());
    }
  int controlWidth = 0;
  for (auto * control : controls)
    {
      control->setMinimumWidth (0);
      control->setMaximumWidth (QWIDGETSIZE_MAX);
      control->ensurePolished ();
      controlWidth = qMax (controlWidth, control->minimumSizeHint ().width ());
    }
  for (auto * label : labels) label->setMinimumWidth (labelWidth);
  for (auto * control : controls) control->setFixedWidth (controlWidth);
}

#endif
