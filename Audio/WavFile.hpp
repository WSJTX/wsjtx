#ifndef WAVFILE_HPP
#define WAVFILE_HPP

#include <QString>
#include "Radio.hpp"

namespace Radio
{

class WavFile
{
public:
  static QString save (QString const& name, short const * data, int samples,
                       QString const& my_callsign, QString const& my_grid,
                       QString const& mode, qint32 sub_mode,
                       Frequency frequency, QString const& his_call,
                       QString const& his_grid, QString const& dgrd);
  static void killWaveFile (QString const& name, bool saveAll, bool saveDecoded,
                            bool bDecoded, QString const& mode);
};

}

#endif // WAVFILE_HPP
