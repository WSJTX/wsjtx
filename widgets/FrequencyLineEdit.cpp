#include "FrequencyLineEdit.hpp"

#include <limits>

#include <QColor>
#include <QDoubleValidator>
#include <QString>
#include <QLocale>

#include "moc_FrequencyLineEdit.cpp"

namespace
{
  double constexpr MHz_factor {1.e6};
  double constexpr minimum_frequency_MHz {1. / MHz_factor};
  QColor const invalid_text_color {180, 0, 0};

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
  , default_palette_ {palette ()}
{
  setValidator (new MHzValidator {minimum_frequency_MHz, static_cast<double>(std::numeric_limits<Radio::Frequency>::max ()) / MHz_factor, this});
  setPlaceholderText (tr ("Frequency in MHz"));
  connect (this, &QLineEdit::textChanged, this, [this] {update_input_feedback ();});
  update_input_feedback ();
}

auto FrequencyLineEdit::frequency () const -> Frequency
{
  return frequency (nullptr);
}

auto FrequencyLineEdit::frequency (bool * ok) const -> Frequency
{
  if (!hasAcceptableInput ())
    {
      if (ok) *ok = false;
      return 0;
    }
  return Radio::frequency (text (), 6, ok);
}

void FrequencyLineEdit::frequency (Frequency f)
{
  setText (Radio::frequency_MHz_string (f));
}

void FrequencyLineEdit::update_input_feedback ()
{
  auto palette = default_palette_;
  if (hasAcceptableInput ())
    {
      setToolTip (tr ("Frequency in MHz"));
    }
  else
    {
      palette.setColor (QPalette::Text, invalid_text_color);
      setToolTip (tr ("Enter a positive frequency in MHz."));
    }
  setPalette (palette);
}
