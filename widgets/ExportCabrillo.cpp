#include "ExportCabrillo.h"

#include <QApplication>
#include <QFileDialog>
#include <QDir>
#include <QDebug>

#include "SettingsGroup.hpp"
#include "Configuration.hpp"
#include "MessageBox.hpp"
#include "models/CabrilloLog.hpp"

#include "ui_ExportCabrillo.h"
#include "moc_ExportCabrillo.cpp"

ExportCabrillo::ExportCabrillo (QSettings * settings, Configuration const * configuration
                                , CabrilloLog const * log, QWidget * parent)
  : QDialog {parent},
    settings_ {settings},
    configuration_ {configuration},
    log_ {log},
    ui {new Ui::ExportCabrillo}
{
  ui->setupUi (this);
  read_settings ();
  setWindowTitle (QApplication::applicationName() + " - Export Cabrillo");
  connect (ui->buttonBox, &QDialogButtonBox::accepted, this, &ExportCabrillo::save_log);
}

ExportCabrillo::~ExportCabrillo ()
{
  write_settings ();
}

void ExportCabrillo::read_settings ()
{
  SettingsGroup group {settings_, "ExportCabrillo"};
  restoreGeometry (settings_->value("window/geometry").toByteArray());
  ui->location_line_edit->setText(settings_->value("Location").toString());
  ui->contest_line_edit->setText(settings_->value("Contest").toString());
  ui->call_line_edit->setText(settings_->value("Callsign").toString());
  ui->category_op_line_edit->setText(settings_->value("Category-Operator").toString());
  ui->category_xmtr_line_edit->setText(settings_->value("Category-Transmitter").toString());
  ui->category_pwr_line_edit->setText(settings_->value("Category-Power").toString());
  ui->category_assisted_line_edit->setText(settings_->value("Category-Assisted").toString());
  ui->category_band_line_edit->setText(settings_->value("Category-Band").toString());
  ui->claimed_line_edit->setText(settings_->value("Claimed-Score").toString());
  ui->operators_line_edit->setText(settings_->value("Operators").toString());
  ui->club_line_edit->setText(settings_->value("Club").toString());
  ui->name_line_edit->setText(settings_->value("Name").toString());
  ui->addr_1_line_edit->setText(settings_->value("Address1").toString());
  ui->addr_2_line_edit->setText(settings_->value("Address2").toString());
}

void ExportCabrillo::write_settings ()
{
  SettingsGroup group {settings_, "ExportCabrillo"};
  settings_->setValue ("window/geometry", saveGeometry ());
  settings_->setValue("Location",ui->location_line_edit->text());
  settings_->setValue("Contest",ui->contest_line_edit->text());
  settings_->setValue("Callsign",ui->call_line_edit->text());
  settings_->setValue("Category-Operator",ui->category_op_line_edit->text());
  settings_->setValue("Category-Transmitter",ui->category_xmtr_line_edit->text());
  settings_->setValue("Category-Power",ui->category_pwr_line_edit->text());
  settings_->setValue("Category-Assisted",ui->category_assisted_line_edit->text());
  settings_->setValue("Category-Band",ui->category_band_line_edit->text());
  settings_->setValue("Claimed-Score",ui->claimed_line_edit->text());
  settings_->setValue("Operators",ui->operators_line_edit->text());
  settings_->setValue("Club",ui->club_line_edit->text());
  settings_->setValue("Name",ui->name_line_edit->text());
  settings_->setValue("Address1",ui->addr_1_line_edit->text());
  settings_->setValue("Address2",ui->addr_2_line_edit->text());
}

void ExportCabrillo::save_log ()
{
  auto fname = QFileDialog::getSaveFileName (this
                                             , tr ("Save Log File")
                                             , configuration_->writeable_data_dir ().absolutePath ()
                                             , tr ("Cabrillo Log (*.cbr)"));
  if (fname.size ())
    {
      QFile f {fname};
      if (f.open (QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out {&f};
        out << "START-OF-LOG:3.0\n"
            << "LOCATION: " << ui->location_line_edit->text() << '\n'
            << "CONTEST: " << ui->contest_line_edit->text() << '\n'
            << "CALLSIGN: " << ui->call_line_edit->text() << '\n'
            << "CATEGORY-OPERATOR: " << ui->category_op_line_edit->text() << '\n'
            << "CATEGORY-TRANSMITTER: " << ui->category_xmtr_line_edit->text() << '\n'
            << "CATEGORY-POWER: " << ui->category_pwr_line_edit->text() << '\n'
            << "CATEGORY-ASSISTED: " << ui->category_assisted_line_edit->text() << '\n'
            << "CATEGORY-BAND: " << ui->category_band_line_edit->text() << '\n'
            << "CLAIMED-SCORE: " << ui->claimed_line_edit->text() << '\n'
            << "OPERATORS: " << ui->operators_line_edit->text() << '\n'
            << "CLUB: " << ui->club_line_edit->text() << '\n'
            << "NAME: " << ui->name_line_edit->text() << '\n'
            << "ADDRESS: " << ui->addr_1_line_edit->text() << '\n'
            << "ADDRESS: " << ui->addr_2_line_edit->text() << '\n';
        if (log_) log_->export_qsos (out);
        out << "END-OF-LOG:" << endl;
        return;
      } else {
        auto const& message = tr ("Cannot open \"%1\" for writing: %2")
          .arg (f.fileName ()).arg (f.errorString ());
        MessageBox::warning_message (this, tr ("Export Cabrillo File Error"), message);
      }
    }
  setResult (Rejected);
}
