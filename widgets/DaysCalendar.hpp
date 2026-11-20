// -*- Mode: C++ -*-
#ifndef WSJTX_WIDGETS_DAYS_CALENDAR_HPP
#define WSJTX_WIDGETS_DAYS_CALENDAR_HPP

#include <QDate>
#include <QSet>
#include <QString>
#include <QVector>
#include <functional>

class QWidget;

namespace DaysCalendar
{
  //! The days, oldest first.
  QVector<QDate> sorted (QSet<QDate> const& days);

  //! "2026-10-31, 2026-11-01" (ISO, oldest first), or none_text if empty.
  QString text (QSet<QDate> const& days, QString const& none_text);

  //! A month calendar popup under anchor for choosing several days:
  //! clicking a day adds it to *days or removes it, then changed () is
  //! called at once. Monday first; today (UTC) is outlined. Closes on
  //! Close, Esc or a click outside it. *days must outlive the popup (it is
  //! a child of anchor). none_text is shown when no day is chosen: what that
  //! means is the caller's to say (every QSO counts; nothing to export ...).
  void show (QWidget * anchor, QSet<QDate> * days, std::function<void ()> changed, QString const& none_text);
}

#endif
