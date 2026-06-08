#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "Configuration.hpp"
#include "models/FrequencyList.hpp"
#include "widgets/BandHopping.hpp"

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
            m_displayBand = false;
            bandHopping();
            startIndex = 0;
            return;
     }
   }
}

void MainWindow::bandHopping()
{
  if (!ui->pbBandHopping->isChecked()
      || ui->autoButton->isChecked()
      || ui->tuneButton->isChecked ())
    {
      return;
    }

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

  std::vector<bool> selected;
  selected.reserve (sizeof entries / sizeof entries[0]);
  for (auto const& entry : entries)
    {
      selected.push_back (entry.checkbox_->isChecked ());
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

  switch (entry.mode_)
    {
    case BandHopMode::FT8:
      on_actionFT8_triggered ();
      break;
    case BandHopMode::FT4:
      on_actionFT4_triggered ();
      break;
    case BandHopMode::MSK144:
      on_actionMSK144_triggered ();
      break;
    case BandHopMode::CustomQRG:
      keep_frequency = true;
      QTimer::singleShot (250, [=] {keep_frequency = false;});
      setRig (frequency);
      on_actionFT8_triggered ();
      ui->pbBandHopping->setChecked (true);
      startIndex = hop_index + 1;
      return;
    }

  auto const& row = m_config.frequencies ()->best_working_frequency (frequency);
  ui->bandComboBox->setCurrentIndex (row);
  if (row >= 0) on_bandComboBox_activated (row);
  ui->pbBandHopping->setChecked (true);
  startIndex = hop_index + 1;
}
