#ifndef RADIO_HPP__
#define RADIO_HPP__

#include <QObject>
#include <QLocale>
#include <QList>

#include "udp_export.h"

class QRegularExpression;
class QVariant;
class QString;

//
// Declarations common to radio software.
//

namespace Radio
{
  //
  // Frequency types
  //
  using Frequency = quint64;
  using Frequencies = QList<Frequency>;
  using FrequencyDelta = qint64;

  //
  // Qt type registration
  //
  void UDP_NO_EXPORT register_types ();

  //
  // Frequency type conversion.
  //
  //	QVariant argument is convertible to double and is assumed to
  //	be scaled by (10 ** -scale).
  //
  Frequency UDP_EXPORT frequency (QVariant const&, int scale = 0,
                                  bool * ok = nullptr, QLocale const& = QLocale ());
  FrequencyDelta UDP_EXPORT frequency_delta (QVariant const&, int scale = 0,
                                             bool * ok = nullptr, QLocale const& = QLocale ());
  Frequency UDP_EXPORT frequency (double, int scale = 0, bool * ok = nullptr);
  FrequencyDelta UDP_EXPORT frequency_delta (double, int scale = 0, bool * ok = nullptr);

  //
  // Frequency type formatting
  //
  QString UDP_EXPORT frequency_MHz_string (Frequency, int precision = 6, QLocale const& = QLocale ());
  QString UDP_EXPORT frequency_MHz_string (FrequencyDelta, int precision = 6, QLocale const& = QLocale ());
  QString UDP_EXPORT pretty_frequency_MHz_string (Frequency, QLocale const& = QLocale ());
  QString UDP_EXPORT pretty_frequency_MHz_string (double, int scale, QLocale const& = QLocale ());
  QString UDP_EXPORT pretty_frequency_MHz_string (FrequencyDelta, QLocale const& = QLocale ());

  //
  // Callsigns
  //
  bool UDP_EXPORT is_standard_callsign (QString const&);
  UDP_EXPORT QRegularExpression const& decoded_grid_pattern ();
  bool UDP_EXPORT is_callsign (QString const&);
  bool UDP_EXPORT is_compound_callsign (QString const&);
  bool UDP_EXPORT is_operating_suffix (QString const&);
  bool is_77bit_nonstandard_callsign (QString const&);
  QString UDP_EXPORT base_callsign (QString);
  QString UDP_EXPORT effective_prefix (QString);
}

Q_DECLARE_METATYPE (Radio::Frequency);
Q_DECLARE_METATYPE (Radio::Frequencies);
Q_DECLARE_METATYPE (Radio::FrequencyDelta);

#endif
