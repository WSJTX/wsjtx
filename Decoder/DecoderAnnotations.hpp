#ifndef WSJTX_DECODER_ANNOTATIONS_HPP
#define WSJTX_DECODER_ANNOTATIONS_HPP

#include <QRegularExpression>
#include <QString>

namespace DecoderAnnotations
{
  struct Metadata
  {
    int ap {0};
    int q {-1};
    int averageCount {0};
    bool lowConfidence {false};
    bool copied {false};
    QString suffix;
  };

  inline QString ap_suffix_pattern ()
  {
    return QStringLiteral (R"((?:\?\s*)?(?:a[0-9][0-9]?|q[0-9][0-9*]?#?))");
  }

  inline QString suffix_pattern ()
  {
    return QStringLiteral (R"((?:\?\s*)?(?:a[0-9][0-9]?|q[0-9][0-9*]?#?|f[0-9*]?|d[0-9*][0-9*]?|[adq]\s[0-9*]))");
  }

  inline Metadata parse (QString suffix)
  {
    Metadata result;
    result.suffix = suffix.trimmed ();
    result.lowConfidence = result.suffix.startsWith ('?');
    if (result.lowConfidence) suffix = result.suffix.mid (1).trimmed ();
    else suffix = result.suffix;
    result.copied = suffix.endsWith ('#');
    if (result.copied) suffix.chop (1);
    auto count = [] (QChar digit) { return digit == '*' ? 10 : digit.digitValue (); };
    if (suffix.size () >= 2 && suffix.startsWith ('a') && suffix.at (1).isDigit ()) result.ap = suffix.at (1).digitValue ();
    if (suffix.size () >= 2 && suffix.startsWith ('q') && suffix.at (1).isDigit ()) result.q = suffix.at (1).digitValue ();
    if (suffix.size () >= 2 && suffix.startsWith ('f')) result.averageCount = count (suffix.at (1));
    else if (suffix.size () >= 3) result.averageCount = count (suffix.at (2));
    return result;
  }

  inline QString normalized_message (QString message)
  {
    static QRegularExpression const suffix {R"((?:^|\s))" + ap_suffix_pattern () + R"(\s*$)"};
    message.remove (suffix);
    message = message.trimmed ();
    // A bare '?' may be decoder confidence or free text; retain the stored text for QSO handling.
    if (message.endsWith ('?')) message.chop (1);
    return message.trimmed ();
  }
}

#endif
