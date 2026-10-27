#ifndef WSJTX_DECODER_ANNOTATIONS_HPP
#define WSJTX_DECODER_ANNOTATIONS_HPP

#include <QRegularExpression>
#include <QString>

namespace DecoderAnnotations
{
  inline QString ap_suffix_pattern ()
  {
    return QStringLiteral (R"((?:\?\s)?(?:a[0-9]|q[0-9][0-9]?))");
  }

  inline QString normalized_message (QString message)
  {
    static QRegularExpression const suffix {ap_suffix_pattern () + R"(\s*$)"};
    message.remove (suffix);
    message = message.trimmed ();
    // A bare '?' may be decoder confidence or free text; retain the stored text for QSO handling.
    if (message.endsWith ('?')) message.chop (1);
    return message.trimmed ();
  }
}

#endif
