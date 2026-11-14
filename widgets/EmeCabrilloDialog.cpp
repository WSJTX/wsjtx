#include "EmeCabrilloDialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

#include "SettingsGroup.hpp"
#include "MessageBox.hpp"
#include "DaysCalendar.hpp"

#include "moc_EmeCabrilloDialog.cpp"

namespace
{
  QString escaped (QString const& text) {return text.toHtmlEscaped ();}

  QString qso_text (EmeCabrillo::Qso const& qso)
  {
    return QString {"%1 %2 %3 %4"}.arg (qso.call, qso.band, qso.date.toString (Qt::ISODate), qso.time);
  }
}

EmeCabrilloDialog::EmeCabrilloDialog (QSettings * settings, QString const& adif_path,
                                      QString const& my_call, QString const& my_grid,
                                      QSet<QDate> const& days, QString const& save_directory,
                                      QWidget * parent)
  : QDialog {parent}
  , settings_ {settings}
  , own_log_ {adif_path}
  , my_call_ {my_call.trimmed ().toUpper ()}
  , my_grid_ {my_grid.trimmed ()}
  , save_directory_ {save_directory}
  , days_ {days}
{
  setWindowTitle (QApplication::applicationName () + " - " + tr ("Cabrillo log for the ARRL EME contest"));
  auto layout = new QVBoxLayout {this};

  // which log: WSJT-X's own, or any ADIF file (only read)
  auto log_row = new QHBoxLayout;
  log_row->addWidget (new QLabel {tr ("Log"), this});
  log_label_ = new QLabel {this};
  log_label_->setTextInteractionFlags (Qt::TextSelectableByMouse);
  log_row->addWidget (log_label_, 1);
  auto other = new QPushButton {tr ("Other file…"), this};
  other->setToolTip (tr ("Make the entry from another ADIF log, e.g. one from a contest PC. It is only read."));
  connect (other, &QPushButton::clicked, this, &EmeCabrilloDialog::choose_file);
  log_row->addWidget (other);
  auto own = new QPushButton {tr ("This log"), this};
  own->setToolTip (tr ("Use WSJT-X's own ADIF log (%1).").arg (QDir::toNativeSeparators (own_log_)));
  connect (own, &QPushButton::clicked, this, &EmeCabrilloDialog::use_own_log);
  log_row->addWidget (own);
  layout->addLayout (log_row);

  // contest days (the same days as Active Stations' "on days") and bands
  auto days_row = new QHBoxLayout;
  days_row->addWidget (new QLabel {tr ("Contest days"), this});
  days_button_ = new QToolButton {this};
  connect (days_button_, &QToolButton::clicked, this, [this] {
      DaysCalendar::show (days_button_, &days_, [this] {
          Q_EMIT days_changed (days_);
          refresh ();
        });
    });
  days_row->addWidget (days_button_);
  days_label_ = new QLabel {this};
  days_label_->setWordWrap (true);
  days_row->addWidget (days_label_, 1);
  layout->addLayout (days_row);
  bands_layout_ = new QHBoxLayout;
  layout->addLayout (bands_layout_);

  // QSOs logged without a grid: all listed, to type them in at once
  grids_box_ = new QGroupBox {tr ("Grids missing in the log - type them in (Enter or Tab)"), this};
  grids_layout_ = new QGridLayout {grids_box_};
  layout->addWidget (grids_box_);

  // the entry's details
  auto entry = new QGroupBox {tr ("Entry"), this};
  auto entry_layout = new QGridLayout {entry};
  auto op_row = new QHBoxLayout;
  single_op_ = new QRadioButton {tr ("Single operator"), entry};
  multi_op_ = new QRadioButton {tr ("Multioperator"), entry};
  op_row->addWidget (single_op_);
  op_row->addWidget (multi_op_);
  op_row->addSpacing (12);
  op_row->addWidget (new QLabel {tr ("Operators"), entry});
  operators_ = new QLineEdit {entry};
  op_row->addWidget (operators_, 1);
  entry_layout->addLayout (op_row, 0, 0, 1, 4);
  auto field = [&] (QLineEdit *& edit, QString const& label, int index, QString const& tip = {}) {
    edit = new QLineEdit {entry};
    edit->setToolTip (tip);
    auto row = 1 + index / 2, column = (index % 2) * 2;
    entry_layout->addWidget (new QLabel {label, entry}, row, column);
    entry_layout->addWidget (edit, row, column + 1);
  };
  field (location_, tr ("Location"), 0, tr ("ARRL section (US and Canada), else DX"));
  field (name_, tr ("Name"), 1);
  field (address_, tr ("Address"), 2);
  field (city_, tr ("City"), 3);
  field (state_, tr ("State/Province"), 4);
  field (postcode_, tr ("Postcode"), 5);
  field (country_, tr ("Country"), 6);
  field (email_, tr ("Email"), 7, tr ("For ARRL's replies"));
  layout->addWidget (entry);

  // summary and the file as it will be saved
  summary_ = new QLabel {this};
  summary_->setWordWrap (true);
  summary_->setTextFormat (Qt::RichText);
  layout->addWidget (summary_);
  preview_ = new QPlainTextEdit {this};
  preview_->setReadOnly (true);
  preview_->setLineWrapMode (QPlainTextEdit::NoWrap);
  preview_->setFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
  preview_->setMinimumHeight (160);
  layout->addWidget (preview_, 1);

  auto buttons = new QHBoxLayout;
  auto hint = new QLabel {tr ("Upload the saved file at contests.arrl.org/arrlemescoresubmission.php"), this};
  hint->setTextInteractionFlags (Qt::TextSelectableByMouse);
  buttons->addWidget (hint, 1);
  save_button_ = new QPushButton {tr ("Save…"), this};
  connect (save_button_, &QPushButton::clicked, this, &EmeCabrilloDialog::save);
  buttons->addWidget (save_button_);
  auto close = new QPushButton {tr ("Close"), this};
  connect (close, &QPushButton::clicked, this, &QDialog::accept);
  buttons->addWidget (close);
  layout->addLayout (buttons);

  read_settings ();
  for (auto edit : {operators_, location_, name_, address_, city_, state_, postcode_, country_, email_})
    connect (edit, &QLineEdit::textChanged, this, &EmeCabrilloDialog::refresh);
  connect (single_op_, &QRadioButton::toggled, this, &EmeCabrilloDialog::refresh);
  load ();
}

EmeCabrilloDialog::~EmeCabrilloDialog ()
{
  write_settings ();
}

void EmeCabrilloDialog::read_settings ()
{
  SettingsGroup group {settings_, "EmeCabrillo"};
  restoreGeometry (settings_->value ("window/geometry").toByteArray ());
  other_log_ = settings_->value ("OtherLog").toString ();
  (settings_->value ("Operator", "SINGLE-OP").toString () == "MULTI-OP" ? multi_op_ : single_op_)->setChecked (true);
  operators_->setText (settings_->value ("Operators").toString ());
  location_->setText (settings_->value ("Location").toString ());
  name_->setText (settings_->value ("Name").toString ());
  address_->setText (settings_->value ("Address").toString ());
  city_->setText (settings_->value ("City").toString ());
  state_->setText (settings_->value ("State").toString ());
  postcode_->setText (settings_->value ("Postcode").toString ());
  country_->setText (settings_->value ("Country").toString ());
  email_->setText (settings_->value ("Email").toString ());
}

void EmeCabrilloDialog::write_settings ()
{
  SettingsGroup group {settings_, "EmeCabrillo"};
  settings_->setValue ("window/geometry", saveGeometry ());
  settings_->setValue ("OtherLog", other_log_);
  settings_->setValue ("Operator", multi_op_->isChecked () ? "MULTI-OP" : "SINGLE-OP");
  settings_->setValue ("Operators", operators_->text ());
  settings_->setValue ("Location", location_->text ());
  settings_->setValue ("Name", name_->text ());
  settings_->setValue ("Address", address_->text ());
  settings_->setValue ("City", city_->text ());
  settings_->setValue ("State", state_->text ());
  settings_->setValue ("Postcode", postcode_->text ());
  settings_->setValue ("Country", country_->text ());
  settings_->setValue ("Email", email_->text ());
}

QString EmeCabrilloDialog::log_path () const
{
  return other_log_.isEmpty () ? own_log_ : other_log_;
}

void EmeCabrilloDialog::choose_file ()
{
  auto const path = QFileDialog::getOpenFileName (this, tr ("ADIF log for the Cabrillo entry (only read)"),
                                                  QFileInfo {log_path ()}.absolutePath (),
                                                  tr ("ADIF log (*.adi *.adif);;All files (*)"));
  if (path.isEmpty ()) return;
  other_log_ = QFileInfo {path} == QFileInfo {own_log_} ? QString {} : path;
  load ();
}

void EmeCabrilloDialog::use_own_log ()
{
  other_log_.clear ();
  load ();
}

void EmeCabrilloDialog::load ()
{
  records_.clear ();
  load_error_.clear ();
  fixes_.clear ();                      // typed grids belong to the log they were typed for
  QFile file {log_path ()};
  if (file.open (QIODevice::ReadOnly))
    records_ = EmeCabrillo::read_adif (file.readAll ());
  else
    load_error_ = tr ("Cannot read %1: %2").arg (QDir::toNativeSeparators (log_path ()), file.errorString ());
  log_label_->setText (QDir::toNativeSeparators (log_path ())
                       + (other_log_.isEmpty () ? QString {} : tr ("  (only read)")));
  refresh ();
}

EmeCabrillo::Header EmeCabrilloDialog::header () const
{
  EmeCabrillo::Header h;
  h.callsign = my_call_;
  h.grid = my_grid_;
  h.category_operator = multi_op_->isChecked () ? "MULTI-OP" : "SINGLE-OP";
  h.operators = multi_op_->isChecked () ? operators_->text () : QString {};
  h.location = location_->text ();
  h.name = name_->text ();
  h.address = address_->text ();
  h.city = city_->text ();
  h.state = state_->text ();
  h.postcode = postcode_->text ();
  h.country = country_->text ();
  h.email = email_->text ();
  h.created_by = "WSJT-X v" + QApplication::applicationVersion ();
  return h;
}

void EmeCabrilloDialog::refresh ()
{
  auto const n = days_.size ();
  days_button_->setText (n == 0 ? tr ("choose…") : n == 1 ? tr ("1 day") : tr ("%1 days").arg (n));
  days_label_->setText (DaysCalendar::text (days_, tr ("(the same days as Active Stations' \"on days\")")));

  // bands with QSOs on the days; those unticked are left out
  auto const all = EmeCabrillo::select (records_, days_, my_call_, my_grid_, {}, fixes_);
  QSet<QString> chosen;
  for (auto const& band : all.bands)
    if (!bands_off_.contains (band)) chosen << band;
  if (chosen.isEmpty () && !all.bands.isEmpty ()) chosen << "-"; // none ticked: nothing
  selection_ = EmeCabrillo::select (records_, days_, my_call_, my_grid_, chosen, fixes_);
  text_ = EmeCabrillo::cabrillo_text (header (), selection_.qsos);
  show_bands ();
  show_grid_rows ();

  QStringList warnings;
  if (!load_error_.isEmpty ()) warnings << load_error_;
  if (my_call_.isEmpty ()) warnings << tr ("My Call is empty (Settings, General).");
  if (EmeCabrillo::grid4 (my_grid_).isEmpty ()) warnings << tr ("My Grid is empty or not a grid (Settings, General).");
  if (days_.isEmpty ()) warnings << tr ("Choose the contest days first.");
  QStringList weekdays;
  for (auto const& day : DaysCalendar::sorted (days_))
    if (day.dayOfWeek () < 6) weekdays << QLocale {QLocale::English}.toString (day, "ddd yyyy-MM-dd");
  if (weekdays.size ())
    warnings << tr ("Not a Saturday or Sunday: %1 (contest weekends run Sat 00:00 - Sun 23:59 UTC).")
      .arg (weekdays.join (", "));
  if (days_.size () && selection_.qsos.isEmpty ()) warnings << tr ("No EME QSOs on these days and bands.");
  if (selection_.no_grid.size ())
    {
      QStringList calls;
      for (auto const& qso : selection_.no_grid) calls << qso.call;
      warnings << tr ("%1 QSO(s) left out until you type their grid in above: %2")
        .arg (selection_.no_grid.size ()).arg (calls.join (", "));
    }

  QString html;
  for (auto const& w : warnings) html += "<p style='color:#c00000'>" + escaped (w) + "</p>";
  if (selection_.qsos.size ())
    {
      auto const s = EmeCabrillo::score (selection_.qsos);
      html += "<table cellspacing='0' cellpadding='1'><tr><th align='left'>" + tr ("Band")
        + "</th><th align='right'>&nbsp;&nbsp;" + tr ("QSOs") + "</th><th align='right'>&nbsp;&nbsp;"
        + tr ("Grids") + "</th></tr>";
      for (auto const& b : s.bands)
        html += QString {"<tr><td>%1</td><td align='right'>%2</td><td align='right'>%3</td></tr>"}
          .arg (b.band).arg (b.qsos).arg (b.grids);
      html += QString {"<tr><td><b>%1</b></td><td align='right'><b>%2</b></td><td align='right'><b>%3</b></td>"
                       "<td>&nbsp;&nbsp;<b>%4</b></td></tr></table>"}
        .arg (tr ("Total")).arg (selection_.qsos.size ()).arg (s.multipliers)
        .arg (tr ("claimed score %1 x %2 = %3").arg (s.points).arg (s.multipliers).arg (s.claimed));
    }
  if (selection_.dupes.size ())
    {
      QStringList dupes;
      for (auto const& qso : selection_.dupes) dupes << qso_text (qso);
      html += "<p>" + escaped (tr ("%1 left out as worked before on the band from the same grid: %2")
                               .arg (selection_.dupes.size ()).arg (dupes.join (", "))) + "</p>";
    }
  if (selection_.satellite)
    html += "<p>" + escaped (tr ("%1 satellite QSO(s) left out (not EME).").arg (selection_.satellite)) + "</p>";
  summary_->setText (html);
  preview_->setPlainText (text_);
  save_button_->setEnabled (selection_.qsos.size () && !my_call_.isEmpty ());
}

void EmeCabrilloDialog::show_bands ()
{
  QSet<QString> shown;
  for (auto i = band_boxes_.cbegin (); i != band_boxes_.cend (); ++i) shown << i.key ();
  if (shown == selection_.bands) return;
  while (auto item = bands_layout_->takeAt (0))
    {
      if (item->widget ()) item->widget ()->deleteLater ();
      delete item;
    }
  band_boxes_.clear ();
  if (selection_.bands.isEmpty ()) return;
  bands_layout_->addWidget (new QLabel {tr ("Bands"), this});
  // lowest band first, in the order of the band table
  QStringList ordered;
  for (auto const& band : selection_.bands) ordered << band;
  std::sort (ordered.begin (), ordered.end (), [] (QString const& a, QString const& b) {
      auto value = [] (QString const& code) {
        return code.endsWith ('G') ? code.chopped (1).toDouble () * 1000. : code.toDouble ();
      };
      return value (a) < value (b);
    });
  for (auto const& band : ordered)
    {
      auto box = new QCheckBox {band, this};
      box->setChecked (!bands_off_.contains (band));
      connect (box, &QCheckBox::toggled, this, [this, band] (bool on) {
          if (on) bands_off_.remove (band); else bands_off_ << band;
          refresh ();
        });
      bands_layout_->addWidget (box);
      band_boxes_[band] = box;
    }
  bands_layout_->addStretch ();
}

void EmeCabrilloDialog::show_grid_rows ()
{
  auto state_of = [] (EmeCabrillo::Qso const& qso, QString& colour) {
    if (qso.entered)
      {
        colour = "#008000";
        return tr ("entered");
      }
    if (qso.filled)
      {
        colour = "#b06000";
        return tr ("%1 from another QSO with %2 - check, or type the right one").arg (qso.grid, qso.call);
      }
    colour = "#c00000";
    return tr ("needed - left out until you type it");
  };
  QStringList keys;
  for (auto const& qso : selection_.need_grid) keys << qso.key;
  if (keys == grid_keys_)
    {
      for (auto const& qso : selection_.need_grid)
        {
          QString colour;
          auto label = grid_states_.value (qso.key);
          label->setText (state_of (qso, colour));
          label->setStyleSheet ("color:" + colour);
        }
      return;
    }
  QHash<QString, QString> typed;
  for (auto i = grid_edits_.cbegin (); i != grid_edits_.cend (); ++i) typed[i.key ()] = i.value ()->text ();
  while (auto item = grids_layout_->takeAt (0))
    {
      if (item->widget ()) item->widget ()->deleteLater ();
      delete item;
    }
  grid_edits_.clear ();
  grid_states_.clear ();
  grid_keys_ = keys;
  grids_box_->setVisible (!keys.isEmpty ());
  auto const fixed = QFontDatabase::systemFont (QFontDatabase::FixedFont);
  int row = 0;
  for (auto const& qso : selection_.need_grid)
    {
      auto what = new QLabel {QString {"%1 %2  %3 %4"}.arg (qso.call, -10).arg (qso.band, 4)
                              .arg (qso.date.toString (Qt::ISODate), qso.time), grids_box_};
      what->setFont (fixed);
      grids_layout_->addWidget (what, row, 0);
      auto edit = new QLineEdit {typed.value (qso.key, fixes_.value (qso.key)), grids_box_};
      edit->setObjectName ("grid_" + qso.call);
      edit->setMaximumWidth (80);
      edit->setFont (fixed);
      auto const key = qso.key;
      connect (edit, &QLineEdit::editingFinished, this, [this, key] {grid_typed (key);});
      grids_layout_->addWidget (edit, row, 1);
      QString colour;
      auto state = new QLabel {state_of (qso, colour), grids_box_};
      state->setStyleSheet ("color:" + colour);
      grids_layout_->addWidget (state, row, 2);
      grid_edits_[key] = edit;
      grid_states_[key] = state;
      ++row;
    }
  grids_layout_->setColumnStretch (2, 1);
}

void EmeCabrilloDialog::grid_typed (QString const& key)
{
  auto edit = grid_edits_.value (key);
  if (!edit) return;
  auto const text = edit->text ().trimmed ().toUpper ();
  static QRegularExpression const grid {QStringLiteral ("\\A[A-R]{2}[0-9]{2}([A-X]{2})?\\z")};
  if (text.isEmpty ())
    {
      fixes_.remove (key);
      edit->setStyleSheet ({});
    }
  else if (grid.match (text).hasMatch () && !EmeCabrillo::grid4 (text).isEmpty ())
    {
      fixes_[key] = text;
      if (edit->text () != text) edit->setText (text);
      edit->setStyleSheet ({});
    }
  else                                  // not a grid: ignored, shown pink
    {
      fixes_.remove (key);
      edit->setStyleSheet ("background:#ffd0d0");
    }
  refresh ();
}

bool EmeCabrilloDialog::save_to (QString const& path)
{
  refresh ();
  QFile file {path};
  if (!file.open (QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
      MessageBox::warning_message (this, tr ("Cabrillo log"), tr ("Cannot open \"%1\" for writing: %2")
                                   .arg (QDir::toNativeSeparators (path), file.errorString ()));
      return false;
    }
  file.write (text_.toLatin1 ());
  return true;
}

void EmeCabrilloDialog::save ()
{
  auto const year = days_.isEmpty () ? QDateTime::currentDateTimeUtc ().date ().year ()
    : DaysCalendar::sorted (days_).last ().year ();
  auto const name = QString {"%1_ARRL-EME_%2.log"}.arg (QString {my_call_}.replace ('/', '-')).arg (year);
  auto const path = QFileDialog::getSaveFileName (this, tr ("Save Cabrillo log"),
                                                  QDir {save_directory_}.absoluteFilePath (name),
                                                  tr ("Cabrillo log (*.log *.cbr);;All files (*)"));
  if (path.isEmpty () || !save_to (path)) return;
  auto const s = EmeCabrillo::score (selection_.qsos);
  QString message = tr ("Saved %1 QSOs (claimed score %2) to\n%3")
    .arg (selection_.qsos.size ()).arg (s.claimed).arg (QDir::toNativeSeparators (path));
  int typed = 0;
  for (auto const& qso : selection_.qsos) typed += qso.entered;
  if (typed)
    message += "\n\n" + tr ("The %1 grid(s) you typed are in the Cabrillo file only: the ADIF log is not changed.")
      .arg (typed);
  message += "\n\n" + tr ("Upload it at contests.arrl.org/arrlemescoresubmission.php.");
  MessageBox::information_message (this, message);
}
