#include "DaysCalendar.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

namespace DaysCalendar
{
  QVector<QDate> sorted (QSet<QDate> const& days)
  {
    QVector<QDate> result {days.begin (), days.end ()};
    std::sort (result.begin (), result.end ());
    return result;
  }

  QString text (QSet<QDate> const& days, QString const& none_text)
  {
    if (days.isEmpty ()) return none_text;
    QStringList result;
    for (auto const& day : sorted (days)) result << day.toString (Qt::ISODate);
    return result.join (", ");
  }

  void show (QWidget * anchor, QSet<QDate> * days, std::function<void ()> changed, QString const& none_text)
  {
    auto popup = new QFrame {anchor, Qt::Popup};
    popup->setAttribute (Qt::WA_DeleteOnClose);
    popup->setFrameStyle (QFrame::Box | QFrame::Plain);
    auto layout = new QVBoxLayout {popup};

    auto header = new QHBoxLayout;
    auto previous = new QToolButton {popup};
    previous->setArrowType (Qt::LeftArrow);
    auto title = new QLabel {popup};
    title->setAlignment (Qt::AlignCenter);
    auto next = new QToolButton {popup};
    next->setArrowType (Qt::RightArrow);
    header->addWidget (previous);
    header->addWidget (title, 1);
    header->addWidget (next);
    layout->addLayout (header);

    auto grid = new QGridLayout;
    grid->setSpacing (2);
    QLocale const english {QLocale::English};
    for (int column = 0; column < 7; ++column)
      {
        auto name = new QLabel {english.dayName (column + 1, QLocale::ShortFormat).left (2), popup};
        name->setAlignment (Qt::AlignCenter);
        grid->addWidget (name, 0, column);
      }
    QVector<QToolButton *> cells;
    for (int i = 0; i < 42; ++i)
      {
        auto cell = new QToolButton {popup};
        cell->setFixedSize (32, 24);
        cell->setAutoRaise (true);
        grid->addWidget (cell, 1 + i / 7, i % 7);
        cells << cell;
      }
    layout->addLayout (grid);

    auto list = new QLabel {popup};
    list->setWordWrap (true);
    list->setMaximumWidth (7 * 34);
    layout->addWidget (list);
    auto buttons = new QHBoxLayout;
    auto clear = new QPushButton {QCoreApplication::translate ("DaysCalendar", "Clear"), popup};
    auto close = new QPushButton {QCoreApplication::translate ("DaysCalendar", "Close"), popup};
    buttons->addWidget (clear);
    buttons->addStretch ();
    buttons->addWidget (close);
    layout->addLayout (buttons);

    // The month shown: that of the newest chosen day, else this month (UTC).
    auto const today = QDateTime::currentDateTimeUtc ().date ();
    auto const chosen = sorted (*days);
    auto const start = chosen.isEmpty () ? today : chosen.last ();
    auto month = std::make_shared<QDate> (start.year (), start.month (), 1);

    auto refresh = [=] {
      title->setText (english.toString (*month, "MMMM yyyy"));
      auto const first = month->addDays (1 - month->dayOfWeek ());
      for (int i = 0; i < cells.size (); ++i)
        {
          auto const day = first.addDays (i);
          auto cell = cells[i];
          bool const inMonth = day.month () == month->month ();
          cell->setText (inMonth ? QString::number (day.day ()) : QString {});
          cell->setEnabled (inMonth);
          cell->setProperty ("day", day);
          QString style;
          if (inMonth && days->contains (day))
            style = "QToolButton{background:#0050c0;color:white;font-weight:bold}";
          else if (inMonth && day == today)
            style = "QToolButton{border:1px solid #0050c0}";
          cell->setStyleSheet (style);
        }
      list->setText (days->isEmpty () ? none_text : text (*days, {}));
    };
    auto on_change = [=] {
      refresh ();
      if (changed) changed ();
    };
    for (auto cell : cells)
      {
        QObject::connect (cell, &QToolButton::clicked, popup, [=] {
            auto const day = cell->property ("day").toDate ();
            if (!days->remove (day)) days->insert (day);
            on_change ();
          });
      }
    QObject::connect (previous, &QToolButton::clicked, popup, [=] {*month = month->addMonths (-1); refresh ();});
    QObject::connect (next, &QToolButton::clicked, popup, [=] {*month = month->addMonths (1); refresh ();});
    QObject::connect (clear, &QPushButton::clicked, popup, [=] {days->clear (); on_change ();});
    QObject::connect (close, &QPushButton::clicked, popup, &QWidget::close);
    refresh ();
    popup->adjustSize ();
    popup->move (anchor->mapToGlobal (QPoint {0, anchor->height ()}));
    popup->show ();
  }
}
