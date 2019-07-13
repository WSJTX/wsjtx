#include "astro.h"

#include <stdio.h>

#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QSettings>
#include <QDateTime>
#include <QTimeZone>
#include <QDir>
#include <QCloseEvent>
#include <QDebug>
#include <math.h>

#include "commons.h"
#include "MessageBox.hpp"
#include "Configuration.hpp"
#include "SettingsGroup.hpp"
#include "qt_helpers.hpp"

#include "ui_astro.h"
#include "moc_astro.cpp"


extern "C" {
  void astrosub(int nyear, int month, int nday, double uth, double freqMoon,
                const char * mygrid, size_t mygrid_len, const char * hisgrid,
                size_t hisgrid_len, double * azsun, double * elsun, double * azmoon,
                double * elmoon, double * azmoondx, double * elmoondx, int * ntsky,
                int * ndop, int * ndop00, double * ramoon, double * decmoon, double * dgrd,
                double * poloffset, double * xnr, double * techo, double * width1,
                double * width2, bool bTx, const char * AzElFileName, size_t AzElFileName_len,
                const char * jpleph, size_t jpleph_len);
}

Astro::Astro(QSettings * settings, Configuration const * configuration, QWidget * parent)
  : QDialog {parent, Qt::WindowTitleHint}
  , settings_ {settings}
  , configuration_ {configuration}
  , ui_ {new Ui::Astro}
  , m_DopplerMethod {0}
  , m_dop {0}
  , m_dop00 {0}
  , m_dx_two_way_dop {0}
{
  ui_->setupUi (this);
  setWindowTitle (QApplication::applicationName () + " - " + tr ("Astronomical Data"));
  setStyleSheet ("QWidget {background: white;}");
  connect (ui_->cbDopplerTracking, &QAbstractButton::toggled, ui_->doppler_widget, &QWidget::setVisible);
  read_settings ();
  ui_->text_label->clear ();
}

Astro::~Astro ()
{
  ui_->cbDopplerTracking->setChecked (false);
  Q_EMIT tracking_update ();
  if (isVisible ()) write_settings ();
}

void Astro::closeEvent (QCloseEvent * e)
{
  write_settings ();
  e->ignore ();                 // do not allow closure by the window system
}

void Astro::read_settings ()
{
  SettingsGroup g (settings_, "Astro");
  ui_->doppler_widget->setVisible (ui_->cbDopplerTracking->isChecked ());
  m_DopplerMethod=settings_->value("DopplerMethod",0).toInt();
  switch (m_DopplerMethod)
    {
    case 0: ui_->rbNoDoppler->setChecked (true); break;
    case 1: ui_->rbFullTrack->setChecked (true); break;
    case 2: ui_->rbConstFreqOnMoon->setChecked (true); break;
    case 3: ui_->rbOwnEcho->setChecked (true); break;
  case 4: ui_->rbOnDxEcho->setChecked (true); break;
  case 5: ui_->rbCallDx->setChecked (true); break;
    }
  move (settings_->value ("window/pos", pos ()).toPoint ());
}

void Astro::write_settings ()
{
  SettingsGroup g (settings_, "Astro");
  //settings_->setValue ("DopplerTracking", ui_->cbDopplerTracking->isChecked ());
  settings_->setValue ("DopplerMethod",m_DopplerMethod);
  settings_->setValue ("window/pos", pos ());
}

auto Astro::astroUpdate(QDateTime const& t, QString const& mygrid, QString const& hisgrid, Frequency freq,
                        bool dx_is_self, bool bTx, bool no_tx_QSY, double TR_period) -> Correction
{
  Frequency freq_moon {freq};
  double azsun,elsun,azmoon,elmoon,azmoondx,elmoondx;
  double ramoon,decmoon,dgrd,poloffset,xnr,techo,width1,width2;
  int ntsky;
  QString date {t.date().toString("yyyy MMM dd").trimmed ()};
  QString utc {t.time().toString().trimmed ()};
  int nyear {t.date().year()};
  int month {t.date().month()};
  int nday {t.date().day()};
  int nhr {t.time().hour()};
  int nmin {t.time().minute()};
  double sec {t.time().second() + 0.001*t.time().msec()};
  double uth {nhr + nmin/60.0 + sec/3600.0};
  if(freq_moon < 1) freq_moon = 144000000;
  auto const& AzElFileName = QDir::toNativeSeparators (configuration_->azel_directory ().absoluteFilePath ("azel.dat"));
  auto const& jpleph = configuration_->data_dir ().absoluteFilePath ("JPLEPH");

  astrosub(nyear, month, nday, uth, static_cast<double> (freq_moon),
           mygrid.toLatin1 ().constData (), mygrid.size (),
           hisgrid.toLatin1().constData(), hisgrid.size (),
           &azsun, &elsun, &azmoon, &elmoon,
           &azmoondx, &elmoondx, &ntsky, &m_dop, &m_dop00, &ramoon, &decmoon,
           &dgrd, &poloffset, &xnr, &techo, &width1, &width2,
           bTx,
           AzElFileName.toLatin1().constData(), AzElFileName.size (),
           jpleph.toLatin1().constData(), jpleph.size ());

    if(!hisgrid.size ()) {
    azmoondx=0.0;
    elmoondx=0.0;
    m_dop=0;
    width2=0.0;
  }
  QString message;
  {
    QTextStream out {&message};
    out << " " << date << "\n"
      "UTC:  " << utc << "\n"
      << fixed
      << qSetFieldWidth (6)
      << qSetRealNumberPrecision (1)
      << "Az:     " << azmoon << "\n"
      "El:     " << elmoon << "\n"
      "SelfDop:" << m_dop00 << "\n"
      "Width:  " << int(width1) << "\n"
      << qSetRealNumberPrecision (2)
      << "Delay:  " << techo << "\n"
      << qSetRealNumberPrecision (1)
      << "DxAz:   " << azmoondx << "\n"
      "DxEl:   " << elmoondx << "\n"
      "DxDop:  " << m_dop << "\n"
      "DxWid:  " << int(width2) << "\n"
      "Dec:    " << decmoon << "\n"
      "SunAz:  " << azsun << "\n"
      "SunEl:  " << elsun << "\n"
      "Freq:   " << freq / 1.e6 << "\n";
    if(freq>=5000000ull) {                     //Suppress data not relevant below VHF
      out << "Tsky:   " << ntsky << "\n"
        "Dpol:   " << poloffset << "\n"
        "MNR:    " << xnr << "\n"
        "Dgrd:   " << dgrd;
    }
  }
  ui_->text_label->setText(message);

  Correction correction;
  if (ui_->cbDopplerTracking->isChecked ()) {
    switch (m_DopplerMethod)
      {
      case 1: // All Doppler correction done here; DX station stays at nominal dial frequency.
	  correction.rx =  m_dop;
	  break;
	  case 4: // All Doppler correction done here; DX station stays at nominal dial frequency. (Trial for OnDxEcho)
	  correction.rx =  m_dop;
	  break;
	  //case 5: // All Doppler correction done here; DX station stays at nominal dial frequency.
	  
      case 3: // Both stations do full correction on Rx and none on Tx
        //correction.rx = dx_is_self ? m_dop00 : m_dop;
		correction.rx =  m_dop00; // Now always sets RX to *own* echo freq
        break;
	  case 2:
        // Doppler correction to constant frequency on Moon
        correction.rx = m_dop00 / 2;
        break;
		
      }
    switch (m_DopplerMethod)
      {
	  case 1: correction.tx = -correction.rx;
	  break;
	  case 2: correction.tx = -correction.rx;
	  break;
	  case 3: correction.tx = 0;
	  break;
	  case 4: // correction.tx = m_dop - m_dop00;
			  
			  correction.tx = m_dx_two_way_dop - m_dop;
			  qDebug () << "correction.tx:" << correction.tx;
	  break;		  
	  case 5: correction.tx = - m_dop00;
	  break;
      }
    //if (3 != m_DopplerMethod || 4 != m_DopplerMethod) correction.tx = -correction.rx;
    
    if(dx_is_self && m_DopplerMethod == 1) correction.rx = 0;

    if (no_tx_QSY && 3 != m_DopplerMethod && 0 != m_DopplerMethod)
      {
       // calculate a single correction for transmit half way through
       // the period as a compromise for rigs that can't CAT QSY
       // while transmitting
        //
        // use a base time of (secs-since-epoch + 2) so as to be sure
        // we do the next period if we calculate just before it starts
        auto sec_since_epoch = t.toMSecsSinceEpoch ()/1000 + 2;
        auto target_sec = sec_since_epoch - fmod(double(sec_since_epoch),TR_period) + 0.5*TR_period;
        auto target_date_time = QDateTime::fromMSecsSinceEpoch (target_sec * 1000, Qt::UTC);
        int nyear {target_date_time.date().year()};
        int month {target_date_time.date().month()};
        int nday {target_date_time.date().day()};
        int nhr {target_date_time.time().hour()};
        int nmin {target_date_time.time().minute()};
        double sec {target_date_time.time().second() + 0.001*target_date_time.time().msec()};
        double uth {nhr + nmin/60.0 + sec/3600.0};
        astrosub(nyear, month, nday, uth, static_cast<double> (freq_moon),
                  mygrid.toLatin1 ().constData (), mygrid.size (),
                  hisgrid.toLatin1().constData(), hisgrid.size (),
                  &azsun, &elsun, &azmoon, &elmoon,
                  &azmoondx, &elmoondx, &ntsky, &m_dop, &m_dop00, &ramoon, &decmoon,
                  &dgrd, &poloffset, &xnr, &techo, &width1, &width2,
                  bTx,
                  AzElFileName.toLatin1().constData(), AzElFileName.size (),
                  jpleph.toLatin1().constData(), jpleph.size ());
        FrequencyDelta offset {0};
        switch (m_DopplerMethod)
          {
          case 1:
            // All Doppler correction done here; DX station stays at nominal dial frequency.
            offset = dx_is_self ? m_dop00 : m_dop;
            break;

          case 2:
            // Doppler correction to constant frequency on Moon
           offset = m_dop00 / 2;
		    break;
			
		case 4:
            // Doppler correction for OnDxEcho
            offset = m_dop - m_dx_two_way_dop;
            break;
			
		//case 5: correction.tx = - m_dop00;
		case 5: offset = m_dop00;// version for _7
			break;
			
			
          }
        correction.tx = -offset;
        qDebug () << "correction.tx (no tx qsy):" << correction.tx;
      }
  }
  return correction;
}

void Astro::check_split ()
{
  if (doppler_tracking () && !configuration_->split_mode ())
    {
      MessageBox::warning_message (this, tr ("Doppler Tracking Error"),
                                   tr ("Split operating is required for Doppler tracking"),
                                   tr ("Go to \"Menu->File->Settings->Radio\" to enable split operation"));
      ui_->rbNoDoppler->click ();
    }
}

void Astro::on_rbFullTrack_clicked()
{
  m_DopplerMethod = 1;
  check_split ();
  Q_EMIT tracking_update ();
}

void Astro::on_rbOnDxEcho_clicked(bool checked)
{
  m_DopplerMethod = 4;
  check_split ();
  if (checked) {
	  m_dx_two_way_dop = 2 * (m_dop - (m_dop00/2));
	  qDebug () << "Starting Doppler:" << m_dx_two_way_dop;
  }
  Q_EMIT tracking_update ();
}

void Astro::on_rbOwnEcho_clicked()
{
  m_DopplerMethod = 3;
  check_split ();
  Q_EMIT tracking_update ();
}

void Astro::on_rbCallDx_clicked()
{
  m_DopplerMethod = 5;
  check_split ();
  Q_EMIT tracking_update ();
}

void Astro::on_rbConstFreqOnMoon_clicked()
{
  m_DopplerMethod = 2;
  check_split ();
  Q_EMIT tracking_update ();
}

void Astro::on_rbNoDoppler_clicked()
{
  m_DopplerMethod = 0;
  Q_EMIT tracking_update ();
}

bool Astro::doppler_tracking () const
{
  return ui_->cbDopplerTracking->isChecked () && m_DopplerMethod;
}

void Astro::on_cbDopplerTracking_toggled(bool)
{
  check_split ();
  Q_EMIT tracking_update ();
}

void Astro::nominal_frequency (Frequency rx, Frequency tx)
{
  ui_->sked_frequency_label->setText (Radio::pretty_frequency_MHz_string (rx));
  ui_->sked_tx_frequency_label->setText (Radio::pretty_frequency_MHz_string (tx));
}

void Astro::hideEvent (QHideEvent * e)
{
  Q_EMIT tracking_update ();
  QWidget::hideEvent (e);
}
