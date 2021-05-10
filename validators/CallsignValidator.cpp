#include "CallsignValidator.hpp"

CallsignValidator::CallsignValidator (QObject * parent, bool allow_compound)
  : QValidator {parent}
  , re_ {allow_compound ? R"(^[A-Z0-9/]+$)" : R"(^[A-Z0-9]+$)"}
{
}

auto CallsignValidator::validate (QString& input, int& pos) const -> State
{
  input = input.toUpper ();
  while (input.size () && input[0].isSpace ())
    {
      input.remove (0, 1);
      if (pos > 0) --pos;
    }
  while (input.size () && input[input.size ()].isSpace ())
    {
      if (pos > input.size ()) --pos;
      input.chop (1);
    }
  auto match = re_.match (input, 0, QRegularExpression::PartialPreferCompleteMatch);
  if (match.hasMatch ()) return Acceptable;
  if (!input.size () || match.hasPartialMatch ()) return Intermediate;
  pos = input.size ();
  return Invalid;
}
