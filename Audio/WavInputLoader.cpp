#include "WavInputLoader.hpp"

#include <algorithm>
#include <cstring>

#include "Audio/WavFile.hpp"

extern "C"
{
  void wav12_ (short d2[], short d1[], int * frames, short * sample_size);
}

namespace
{
int constexpr wav12_output_capacity = 60 * 12000;
}

namespace Radio
{

WavInputResult load_wav_input (QString const& name, int sample_limit)
{
  WavInputResult result;
  auto const basename = name.mid (name.lastIndexOf ('/') + 1);
  auto const wav_position = name.indexOf (".wav", 0, Qt::CaseInsensitive);
  auto const case_sensitive_position = name.indexOf (".wav");
  auto const directory_position = name.lastIndexOf ('/');
  if (wav_position > 0)
    {
      if (case_sensitive_position - directory_position > 13)
        {
          result.nutc = basename.mid (7, 6).toInt ();
          result.fileDateTime = basename.mid (0, 13);
        }
      else if (wav_position == name.indexOf ('_', -11) + 7)
        {
          result.nutc = name.mid (wav_position - 6, 6).toInt ();
          result.fileDateTime = name.mid (wav_position - 13, 13);
        }
      else
        {
          result.nutc = 100 * name.mid (wav_position - 4, 4).toInt ();
          result.fileDateTime = name.mid (wav_position - 11, 11);
        }
    }

  auto const wav = WavFile::load (name, sample_limit);
  if (wav.isValid ())
    {
      result.samples.assign (sample_limit, 0);
      if (!wav.samples.isEmpty ())
        {
          std::memcpy (result.samples.data (), wav.samples.constData (), wav.samples.size ());
        }

      auto frames_read = wav.frames;
      if (11025 == wav.format.sampleRate ())
        {
          result.samples.resize (std::max (sample_limit, wav12_output_capacity), 0);
          short sample_size = wav.format.sampleSize ();
          wav12_ (result.samples.data (), result.samples.data (), &frames_read, &sample_size);
          frames_read = std::min (frames_read, sample_limit);
          result.samples.resize (sample_limit);
        }
      result.frames = frames_read;
      result.valid = true;
    }

  result.yymmdd = basename.left (6).toInt ();
  return result;
}

}
