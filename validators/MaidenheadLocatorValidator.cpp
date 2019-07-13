#include "MaidenheadLocatorValidator.hpp"

MaidenheadLocatorValidator::MaidenheadLocatorValidator (QObject * parent, Length length, Length required)
  : QValidator {parent}
{
  switch (length)
    {
    case Length::field:
      re_.setPattern ({R"(^(?<field>[A-Ra-r]{2})$)"});
      break;
    case Length::square:
      if (Length::field == required)
        {
          re_.setPattern ({R"(^(?<field>[A-Ra-r]{2})([0-9]{2}){0,1}$)"});
        }
      else
        {
          re_.setPattern ({R"(^(?<field>[A-Ra-r]{2})[0-9]{2}$)"});
        }
      break;
    case Length::subsquare:
      if (Length::field == required)
        {
          re_.setPattern ({R"(^(?<field>[A-Ra-r]{2})([0-9]{2}((?<subsquare>[A-Xa-x]{2}){0,1})){0,1}$)"});
        }
      else if (Length::square == required)
        {
          re_.setPattern ({R"(^(?<field>[A-Ra-r]{2})[0-9]{2}(?<subsquare>[A-Xa-x]{2}){0,1}$)"});
        }
      else
        {
          re_.setPattern ({R"(^(?<field>[A-Ra-r]{2})[0-9]{2}(?<subsquare>[A-Xa-x]{2})$)"});
        }
      break;
    case Length::extended:
      if (Length::field == required)
        {
          re_.setPattern ({R"(^(?<field>[A-Ra-r]{2})([0-9]{2}((?<subsquare>[A-Xa-x]{2}){0,1}([0-9]{2}){0,1})){0,1}$)"});
        }
      else if (Length::square == required)
        {
          re_.setPattern ({R"(^(?<field>[A-Ra-r]{2})[0-9]{2}((?<subsquare>[A-Xa-x]{2})([0-9]{2}){0,1}){0,1}$)"});
        }
      else if (Length::subsquare == required)
        {
          re_.setPattern ({R"(^(?<field>[A-Ra-r]{2})[0-9]{2}(?<subsquare>[A-Xa-x]{2})([0-9]{2}){0,1}$)"});
        }
      else
        {
          re_.setPattern ({R"(^(?<field>[A-Ra-r]{2})[0-9]{2}(?<subsquare>[A-Xa-x]{2})[0-9]{2}$)"});
        }
      break;
    }
}

auto MaidenheadLocatorValidator::validate (QString& input, int& pos) const -> State
{
  auto match = re_.match (input, 0, QRegularExpression::PartialPreferCompleteMatch);
  auto field = match.captured ("field");
  if (field.size ())
    {
      input.replace (match.capturedStart ("field"), match.capturedLength ("field"), field.toUpper ());
    }
  auto subsquare = match.captured ("subsquare");
  if (subsquare.size ())
    {
      input.replace (match.capturedStart ("subsquare"), match.capturedLength ("subsquare"), subsquare.toUpper ());
    }
  if (match.hasMatch ()) return Acceptable;
  if (!input.size () || match.hasPartialMatch ()) return Intermediate;
  pos = input.size ();
  return Invalid;
}
