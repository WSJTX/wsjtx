#ifndef CALLSIGN_VALIDATOR_HPP__
#define CALLSIGN_VALIDATOR_HPP__

#include <QValidator>
#include <QRegularExpression>

//
// CallsignValidator - QValidator implementation for callsigns
//
class CallsignValidator final
  : public QValidator
{
public:
  CallsignValidator (QObject * parent = nullptr, bool allow_compound = true);
  static QString normalizeStoredInput (QString input, int max_length,
                                       bool allow_compound = true);

  // QValidator implementation
  State validate (QString& input, int& pos) const override;

private:
  QRegularExpression re_;
};

#endif
