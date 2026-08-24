#include "CallsignValidator.hpp"

CallsignValidator::CallsignValidator (QObject * parent, bool allow_compound)
  : QValidator {parent}
  , re_ {allow_compound ? R"(^[A-Z0-9/]+$)" : R"(^[A-Z0-9]+$)"}
{
}

QString CallsignValidator::normalizeStoredInput (QString input, int max_length,
                                                 bool allow_compound)
{
  input = input.trimmed ();
  input.truncate (max_length);
  CallsignValidator validator {nullptr, allow_compound};
  auto pos = input.size ();
  if (Invalid == validator.validate (input, pos)) input.clear ();
  return input;
}

auto CallsignValidator::validate (QString& input, int& pos) const -> State
{
  input = input.toUpper ();
  while (input.size () && input[0].isSpace ())
    {
      input.remove (0, 1);
      if (pos > 0) --pos;
    }
  while (input.size () && input[input.size () - 1].isSpace ())
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
