#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "Configuration.hpp"
#include "models/Bands.hpp"
#include "models/FrequencyList.hpp"
#include "models/Modes.hpp"
#include "widgets/BandHopping.hpp"
#include "widegraph.h"

#include <cmath>
#include <cstddef>
#include <limits>

#include <QDateTime>
#include <QTimer>

extern bool m_displayBand;
extern bool keep_frequency;

namespace
{
  enum class BandHopMode
  {
    FT8,
    FT4,
    MSK144,
    CustomQRG
  };

  struct BandHopEntry
  {
    QAbstractButton * checkbox_;
    BandHopMode mode_;
    int frequency_;
    QSpinBox * custom_frequency_;
  };

  Radio::Frequency resolveBandHopFrequency (FrequencyList_v2_101 const& frequencies,
                                            Bands const& bands,
                                            Radio::Frequency requested,
                                            IARURegions::Region region,
                                            Modes::Mode mode)
  {
    auto const target_band = bands.find (requested);
    if (target_band.isEmpty ()) return 0;

    auto const now = QDateTime::currentDateTimeUtc ();
    Radio::Frequency result {0};
    Radio::FrequencyDelta delta {std::numeric_limits<Radio::FrequencyDelta>::max ()};
    for (auto const& candidate : frequencies.frequency_list ())
      {
        if (region != IARURegions::ALL
            && candidate.region_ != IARURegions::ALL
            && candidate.region_ != region) continue;
        if (candidate.mode_ != Modes::ALL && candidate.mode_ != mode) continue;
        if (candidate.start_time_.isValid () && candidate.start_time_ > now) continue;
        if (candidate.end_time_.isValid () && candidate.end_time_ < now) continue;
        if (bands.find (candidate.frequency_) != target_band) continue;
        if (candidate.preferred_) return candidate.frequency_;

        Radio::FrequencyDelta const candidate_delta =
          static_cast<Radio::FrequencyDelta> (requested)
          - static_cast<Radio::FrequencyDelta> (candidate.frequency_);
        if (std::abs (candidate_delta) < std::abs (delta))
          {
            delta = candidate_delta;
            result = candidate.frequency_;
          }
      }
    return result;
  }
}

void MainWindow::bandHoppingTimer()
{
    if(ui->pbBandHopping->isChecked()) {
    static int startIndex = 0;
    int nextStartIndex = startIndex +1;
    switch (startIndex){
    case 0:
            startIndex = nextStartIndex;  // band hopping every other minute
            return;
    case 1:
            bandHopping();
            startIndex = 0;
            return;
     }
   }
}

void MainWindow::bandHopping(bool user_requested)
{
  if (!ui->pbBandHopping->isChecked()) return;
  bool const schedule_blocked = ui->autoButton->isChecked() || ui->tuneButton->isChecked ();

  static int startIndex = 0;

  BandHopEntry entries[] =
    {
      {ui->cb160m, BandHopMode::FT8, 1840000, nullptr},
      {ui->cb80m, BandHopMode::FT8, 3573000, nullptr},
      {ui->cb60m, BandHopMode::FT8, 5357000, nullptr},
      {ui->cb40m, BandHopMode::FT8, 7074000, nullptr},
      {ui->cb30m, BandHopMode::FT8, 10136000, nullptr},
      {ui->cb20m, BandHopMode::FT8, 14074000, nullptr},
      {ui->cb17m, BandHopMode::FT8, 18100000, nullptr},
      {ui->cb15m, BandHopMode::FT8, 21074000, nullptr},
      {ui->cb12m, BandHopMode::FT8, 24915000, nullptr},
      {ui->cb10m, BandHopMode::FT8, 28074000, nullptr},
      {ui->cb6m, BandHopMode::FT8, 50313000, nullptr},
      {ui->cb4m, BandHopMode::FT8, 70154000, nullptr},
      {ui->cb2m, BandHopMode::FT8, 144174000, nullptr},
      {ui->cb70cm, BandHopMode::FT8, 432174000, nullptr},
      {ui->cb80mFT4, BandHopMode::FT4, 3575000, nullptr},
      {ui->cb40mFT4, BandHopMode::FT4, 7047500, nullptr},
      {ui->cb30mFT4, BandHopMode::FT4, 10140000, nullptr},
      {ui->cb20mFT4, BandHopMode::FT4, 14080000, nullptr},
      {ui->cb17mFT4, BandHopMode::FT4, 18104000, nullptr},
      {ui->cb15mFT4, BandHopMode::FT4, 21140000, nullptr},
      {ui->cb12mFT4, BandHopMode::FT4, 24919000, nullptr},
      {ui->cb10mFT4, BandHopMode::FT4, 28180000, nullptr},
      {ui->cb2mMSK, BandHopMode::MSK144, 144360000, nullptr},
      {ui->cbQRG1, BandHopMode::CustomQRG, 0, ui->sbQRG1},
      {ui->cbQRG2, BandHopMode::CustomQRG, 0, ui->sbQRG2},
      {ui->cbQRG3, BandHopMode::CustomQRG, 0, ui->sbQRG3},
      {ui->cbQRG4, BandHopMode::CustomQRG, 0, ui->sbQRG4},
      {ui->cbQRG5, BandHopMode::CustomQRG, 0, ui->sbQRG5},
      {ui->cbQRG6, BandHopMode::CustomQRG, 0, ui->sbQRG6},
      {ui->cbQRG7, BandHopMode::CustomQRG, 0, ui->sbQRG7},
      {ui->cbQRG8, BandHopMode::CustomQRG, 0, ui->sbQRG8},
    };

  auto const entry_count = sizeof entries / sizeof entries[0];
  std::vector<bool> selected (entry_count);
  for (std::size_t index = 0; index < entry_count; ++index)
    {
      selected[index] = entries[index].checkbox_->isChecked ();
    }

  // Checkboxes are live controls; a user can leave the active set empty while
  // band hopping remains on, so the scan must terminate without recursive wrap.
  auto const hop_index = next_band_hop_index (selected, startIndex);
  if (hop_index < 0)
    {
      startIndex = 0;
      showStatusMessage (tr ("Band hopping has no selected frequencies."));
      return;
    }

  auto const& entry = entries[hop_index];
  auto const frequency = BandHopMode::CustomQRG == entry.mode_
    ? entry.custom_frequency_->value () * 1000
    : entry.frequency_;

  auto const origin = user_requested
    ? FrequencyRequestOrigin::User
    : FrequencyRequestOrigin::Automatic;
  auto const skip_hop = [&] {
    if (!user_requested) startIndex = hop_index + 1;
  };
  if (!nominalFrequencyChangeAllowed (origin))
    {
      skip_hop ();
      return;
    }
  if (schedule_blocked)
    {
      skip_hop ();
      return;
    }
  if (entry.mode_ == BandHopMode::CustomQRG)
    {
      if (!m_monitoring) monitor (true);
      keep_frequency = true;
      if (!requestNominalFrequencyChange (frequency, origin))
        {
          keep_frequency = false;
          skip_hop ();
          return;
        }
      m_displayBand = false;
      QTimer::singleShot (250, [=] {keep_frequency = false;});
      setXIT (ui->TxFreqSpinBox->value ());
      on_actionFT8_triggered ();
      ui->pbBandHopping->setChecked (true);
      startIndex = hop_index + 1;
      return;
    }

  Modes::Mode mode {Modes::FT8};
  if (entry.mode_ == BandHopMode::FT4) mode = Modes::FT4;
  if (entry.mode_ == BandHopMode::MSK144) mode = Modes::MSK144;
  auto const requested_frequency = resolveBandHopFrequency (
    *m_config.frequencies (), *m_config.bands (), frequency, m_config.region (), mode);
  if (!requested_frequency)
    {
      skip_hop ();
      return;
    }

  if (!m_monitoring) monitor (true);
  auto const previous_frequency = m_freqNominal;
  if (!requestNominalFrequencyChange (requested_frequency, origin))
    {
      skip_hop ();
      return;
    }

  m_displayBand = false;
  keep_frequency = true;
  switch (entry.mode_)
    {
    case BandHopMode::FT8: on_actionFT8_triggered (); break;
    case BandHopMode::FT4: on_actionFT4_triggered (); break;
    case BandHopMode::MSK144: on_actionMSK144_triggered (); break;
    case BandHopMode::CustomQRG: Q_UNREACHABLE ();
    }
  keep_frequency = false;

  auto const& row = m_config.frequencies ()->best_working_frequency (requested_frequency);
  if (row >= 0) ui->bandComboBox->setCurrentIndex (row);
  m_bandEdited = true;
  applyBandChange (requested_frequency, previous_frequency);
  setXIT (ui->TxFreqSpinBox->value ());
  m_wideGraph->setRxBand (m_config.bands ()->find (requested_frequency));
  ui->pbBandHopping->setChecked (true);
  startIndex = hop_index + 1;
}
