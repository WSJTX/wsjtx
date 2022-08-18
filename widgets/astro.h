// -*- Mode: C++ -*-
#ifndef ASTRO_H
#define ASTRO_H

#include <utility>

#include <QDialog>
#include <QScopedPointer>

#include "Radio.hpp"

class QSettings;
class Configuration;
namespace Ui {
  class Astro;
}

class Astro final
  : public QDialog
{
  Q_OBJECT;

public:
  using Frequency = Radio::Frequency;
  using FrequencyDelta = Radio::FrequencyDelta;

  explicit Astro(QSettings * settings, Configuration const *, QWidget * parent = nullptr);
  ~Astro ();

  struct Correction
  {
    Correction ()
      : rx {0}
      , tx {0}
      , dop {0}
      , width {0}
    {}
    Correction (Correction const&) = default;
    Correction& operator = (Correction const&) = default;

    // testing facility used to test Doppler corrections on
    // terrestrial links
    void reverse ()
    {
      std::swap (rx, tx);
    }

    FrequencyDelta rx;
    FrequencyDelta tx;
    double dop;
    double width;
  };

  Correction astroUpdate(QDateTime const& t,
                         QString const& mygrid,
                         QString const& hisgrid,
                         Frequency frequency,
                         bool dx_is_self,
                         bool bTx,
                         bool bAuto,
                         bool no_tx_QSY,
                         double TR_period);

  bool doppler_tracking () const;
  bool bDither();
  void selectOwnEcho();
  void selectOnDxEcho();
  qint32 nfRIT();
  qint32 DopplerMethod();

  Q_SLOT void nominal_frequency (Frequency rx, Frequency tx);
  Q_SIGNAL void tracking_update () const;

protected:
  void hideEvent (QHideEvent *) override;
  void closeEvent (QCloseEvent *) override;

private slots:
  void on_rbConstFreqOnMoon_clicked(bool);
  void on_rbFullTrack_clicked(bool);
  void on_rbNoDoppler_clicked(bool);
  void on_rbOwnEcho_clicked(bool);
  void on_rbOnDxEcho_clicked(bool);
  void on_rbCallDx_clicked(bool);
  void on_cbDopplerTracking_toggled(bool);

private:
  void read_settings ();
  void write_settings ();
  void check_split ();

  QSettings * settings_;
  Configuration const * configuration_;
  QScopedPointer<Ui::Astro> ui_;

  qint32 m_DopplerMethod;
  int m_dop;
  int m_dop00;
  //int m_dx_two_way_dop;
};

inline
bool operator == (Astro::Correction const& lhs, Astro::Correction const& rhs)
{
  return lhs.rx == rhs.rx && lhs.tx == rhs.tx;
}

inline
bool operator != (Astro::Correction const& lhs, Astro::Correction const& rhs)
{
  return !(lhs == rhs);
}

#endif // ASTRO_H
