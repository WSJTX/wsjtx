// -*- Mode: C++ -*-
#ifndef ASTRO_H
#define ASTRO_H

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
    {}
    Correction (Correction const&) = default;
    Correction& operator = (Correction const&) = default;

    FrequencyDelta rx;
    FrequencyDelta tx;
  };
  Correction astroUpdate(QDateTime const& t,
                         QString const& mygrid,
                         QString const& hisgrid,
                         Frequency frequency,
                         bool dx_is_self,
                         bool bTx,
                         bool no_tx_QSY,
                         double TR_period);

  bool doppler_tracking () const;
  Q_SLOT void nominal_frequency (Frequency rx, Frequency tx);
  Q_SIGNAL void tracking_update () const;

protected:
  void hideEvent (QHideEvent *) override;
  void closeEvent (QCloseEvent *) override;

private slots:
  void on_rbConstFreqOnMoon_clicked();
  void on_rbFullTrack_clicked();
  void on_rbOwnEcho_clicked();
  void on_rbNoDoppler_clicked();
  void on_rbOnDxEcho_clicked(bool);
  void on_rbCallDx_clicked();
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
  int m_dx_two_way_dop;
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
