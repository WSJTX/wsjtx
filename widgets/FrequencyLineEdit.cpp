#include "FrequencyLineEdit.hpp"

#include <limits>

#include <QDoubleValidator>
#include <QString>
#include <QLocale>

#include "moc_FrequencyLineEdit.cpp"

namespace
{
  class MHzValidator
    : public QDoubleValidator
  {
  public:
    MHzValidator (double bottom, double top, QObject * parent = nullptr)
      : QDoubleValidator {bottom, top, 6, parent}
    {
    }

    State validate (QString& input, int& pos) const override
    {
      State result = QDoubleValidator::validate (input, pos);
      if (Acceptable == result)
        {
          bool ok;
          (void)QLocale {}.toDouble (input, &ok);
          if (!ok)
            {
              result = Intermediate;
            }
        }
      return result;
    }
  };
}

FrequencyLineEdit::FrequencyLineEdit (QWidget * parent)
  : QLineEdit (parent)
{
  setValidator (new MHzValidator {0., static_cast<double>(std::numeric_limits<Radio::Frequency>::max ()) / 10.e6, this});
}

auto FrequencyLineEdit::frequency () const -> Frequency
{
  return Radio::frequency (text (), 6);
}

void FrequencyLineEdit::frequency (Frequency f)
{
  setText (Radio::frequency_MHz_string (f));
}
