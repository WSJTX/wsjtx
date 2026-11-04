#include <array>
#include <future>
#include <vector>

extern "C"
{
  using WaveGenerator = void (int const *, int const *, int const *, float const *,
                             float const *, float const *, void *, float *,
                             int const *, int *);
  WaveGenerator gen_ft8wave_;
  WaveGenerator gen_ft8wavevar_;
  WaveGenerator gen_jttywave_;
  void gen_ft4wave_ (int const *, int const *, int const *, float const *, float const *,
                    void *, float *, int const *, int const *);
}

namespace
{
  void renderFt4 (int const * tones, int const * symbols, int const * nsps, float const *,
                  float const * rate, float const * frequency, void * complexOutput,
                  float * realOutput, int const * kind, int * samples)
  {
    gen_ft4wave_ (tones, symbols, nsps, rate, frequency, complexOutput, realOutput, kind, samples);
  }

  struct RenderCase
  {
    WaveGenerator * generator;
    int nsps;
    float bt;
    float rate;
    float frequency;
    int kind;
  };

  std::vector<float> render (RenderCase const& config)
  {
    std::array<int, 5> const tones {6, 7, 2, 5, 3};
    int const symbols = int (tones.size ());
    int samples = (symbols + (config.generator == renderFt4 ? 2 : 0)) * config.nsps;
    std::vector<float> output ((config.kind ? 2 : 1) * samples);
    std::vector<float> unused (2 * samples);
    config.generator (tones.data (), &symbols, &config.nsps, &config.bt,
                      &config.rate, &config.frequency,
                      config.kind ? output.data () : unused.data (),
                      config.kind ? unused.data () : output.data (), &config.kind, &samples);
    return output;
  }
}

extern "C" int check_parallel_waveforms ()
{
  std::vector<RenderCase> cases;
  for (auto generator : {gen_ft8wave_, gen_ft8wavevar_, gen_jttywave_})
    for (int kind : {0, 1})
      for (float bt : {1.0f, 2.0f, 8.0f, 1.5f})
        {
          cases.push_back ({generator, 1920, bt, 12000, 1500, kind});
          cases.push_back ({generator, 7680, bt, 48000, -500, kind});
          cases.push_back ({generator, 257, bt, 12000, 12345, kind});
        }
  for (int kind : {0, 1})
    {
      cases.push_back ({renderFt4, 576, 1, 12000, 1500, kind});
      cases.push_back ({renderFt4, 2304, 1, 48000, -500, kind});
      cases.push_back ({renderFt4, 257, 1, 12000, 12345, kind});
    }

  std::vector<std::vector<float>> reference;
  for (auto const& config : cases) reference.push_back (render (config));

  std::promise<void> start;
  auto ready = start.get_future ().share ();
  std::array<std::future<bool>, 4> workers;
  for (unsigned worker = 0; worker < workers.size (); ++worker)
    workers[worker] = std::async (std::launch::async, [&, worker, ready]
      {
        ready.wait ();
        for (unsigned trial = 0; trial < 72; ++trial)
          {
            auto const index = (trial * 7 + worker * 11) % cases.size ();
            if (render (cases[index]) != reference[index]) return false;
          }
        return true;
      });
  start.set_value ();
  bool passed = true;
  for (auto& worker : workers) passed = worker.get () && passed;
  return passed ? 1 : 0;
}
