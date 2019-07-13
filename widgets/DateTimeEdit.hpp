#ifndef DATE_TIME_EDIT_HPP_
#define DATE_TIME_EDIT_HPP_

#include <QDateTimeEdit>

class QWidget;

//
// DateTimeEdit - format includes seconds
//
class DateTimeEdit final
  : public QDateTimeEdit
{
public:
  explicit DateTimeEdit (QWidget * parent = nullptr)
    : QDateTimeEdit {parent}
  {
    setDisplayFormat (locale ().dateFormat (QLocale::ShortFormat) + " hh:mm:ss");
    setTimeSpec (Qt::UTC);
  }
};

#endif
