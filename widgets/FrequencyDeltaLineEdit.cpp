#include "FrequencyDeltaLineEdit.hpp"

#include <limits>

#include <QColor>
#include <QDoubleValidator>
#include <QString>
#include <QLocale>

#include "moc_FrequencyDeltaLineEdit.cpp"

namespace
{
  double constexpr MHz_factor {1.e6};
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

FrequencyDeltaLineEdit::FrequencyDeltaLineEdit (QWidget * parent)
  : QLineEdit (parent)
  , default_palette_ {palette ()}
{
  setValidator (new MHzValidator {static_cast<double>(std::numeric_limits<FrequencyDelta>::min ()) / MHz_factor,
        static_cast<double>(std::numeric_limits<FrequencyDelta>::max ()) / MHz_factor, this});
  setPlaceholderText (tr ("Offset in MHz"));
  connect (this, &QLineEdit::textChanged, this, [this] {update_input_feedback ();});
  update_input_feedback ();
}

auto FrequencyDeltaLineEdit::frequency_delta () const -> FrequencyDelta
{
  return frequency_delta (nullptr);
}

auto FrequencyDeltaLineEdit::frequency_delta (bool * ok) const -> FrequencyDelta
{
  if (!hasAcceptableInput ())
    {
      if (ok) *ok = false;
      return 0;
    }
  return Radio::frequency_delta (text (), 6, ok);
}

void FrequencyDeltaLineEdit::frequency_delta (FrequencyDelta d)
{
  setText (Radio::frequency_MHz_string (d));
}

void FrequencyDeltaLineEdit::update_input_feedback ()
{
  auto palette = default_palette_;
  if (hasAcceptableInput ())
    {
      setToolTip (tr ("Frequency offset in MHz"));
    }
  else
    {
      palette.setColor (QPalette::Text, invalid_text_color);
      setToolTip (tr ("Enter a frequency offset in MHz."));
    }
  setPalette (palette);
}
