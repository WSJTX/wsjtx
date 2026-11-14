#include "EmeCabrilloDialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
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
#include <QScrollArea>
#include <QSettings>
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
  , own_log_path_ {adif_path}
  , my_call_ {my_call.trimmed ().toUpper ()}
  , my_grid_ {my_grid.trimmed ()}
  , save_directory_ {save_directory}
  , days_ {days}
{
  setWindowTitle (QApplication::applicationName () + " - " + tr ("Cabrillo log for the ARRL EME contest"));
  // everything but the Save/Close row scrolls, so the buttons stay in view
  // however small the window or long the list of missing grids
  auto outer = new QVBoxLayout {this};
  auto scroll = new QScrollArea {this};
  scroll->setWidgetResizable (true);
  scroll->setFrameShape (QFrame::NoFrame);
  auto content = new QWidget {scroll};
  auto layout = new QVBoxLayout {content};
  layout->setContentsMargins (0, 0, 0, 0);
  scroll->setWidget (content);
  outer->addWidget (scroll, 1);

  // the logs: WSJT-X's own and/or other ADIF files (e.g. one from each PC
  // of a multi-operator station), merged; all only read
  auto logs_box = new QGroupBox {tr ("Logs (several are merged into one entry; the same QSO in two of them counts once)"),
                                 this};
  auto logs_box_layout = new QVBoxLayout {logs_box};
  logs_layout_ = new QGridLayout;
  logs_box_layout->addLayout (logs_layout_);
  auto logs_buttons = new QHBoxLayout;
  auto other = new QPushButton {tr ("Add file…"), logs_box};
  other->setToolTip (tr ("Add another ADIF log, e.g. one from another PC of the station. It is only read."));
  connect (other, &QPushButton::clicked, this, &EmeCabrilloDialog::choose_file);
  logs_buttons->addWidget (other);
  add_own_button_ = new QPushButton {tr ("Add WSJT-X's log"), logs_box};
  add_own_button_->setToolTip (tr ("Add WSJT-X's own ADIF log (%1).").arg (QDir::toNativeSeparators (own_log_path_)));
  connect (add_own_button_, &QPushButton::clicked, this, [this] {add_log (own_log ());});
  logs_buttons->addWidget (add_own_button_);
  logs_buttons->addWidget (new QLabel {tr ("Every log is only read."), logs_box}, 1);
  logs_box_layout->addLayout (logs_buttons);
  layout->addWidget (logs_box);

  // contest days (the same days as Active Stations' "on days") and bands
  auto days_row = new QHBoxLayout;
  days_row->addWidget (new QLabel {tr ("Contest days"), this});
  days_button_ = new QToolButton {this};
  connect (days_button_, &QToolButton::clicked, this, [this] {
      DaysCalendar::show (days_button_, &days_, [this] {
          Q_EMIT days_changed (days_);
          count_logs ();                // each log's "on the days" count
          show_logs ();
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
  // the modes found on the days and bands, with their QSO counts: the ADIF
  // log holds every mode, so QSOs that were not EME (say FT8 or MSK144 on
  // 2 m) can be left out
  modes_layout_ = new QHBoxLayout;
  layout->addLayout (modes_layout_);
  // shown only when a weekend has QSOs of the other band group
  off_weekend_box_ = new QCheckBox {tr ("Leave out QSOs on a band whose group (50-1296 MHz / 2.3 GHz and up) "
                                        "isn't contested on that weekend"), this};
  off_weekend_box_->setToolTip (tr ("A weekend's band group is the one with more QSOs on it."));
  off_weekend_box_->setVisible (false);
  layout->addWidget (off_weekend_box_);

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
  outer->addLayout (buttons);

  read_settings ();
  for (auto edit : {operators_, location_, name_, address_, city_, state_, postcode_, country_, email_})
    connect (edit, &QLineEdit::textChanged, this, &EmeCabrilloDialog::refresh);
  connect (single_op_, &QRadioButton::toggled, this, &EmeCabrilloDialog::refresh);
  connect (off_weekend_box_, &QCheckBox::toggled, this, &EmeCabrilloDialog::refresh);
  // Enter in a grid box must only take the grid, never press a button
  for (auto button : findChildren<QPushButton *> ())
    {
      button->setAutoDefault (false);
      button->setDefault (false);
    }
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
  // "Logs": the list ("<none>" = empty); first time: WSJT-X's own log
  if (settings_->contains ("Logs"))
    {
      logs_ = settings_->value ("Logs").toStringList ();
      logs_.removeAll (QStringLiteral ("<none>"));
    }
  else
    {
      logs_ = QStringList {own_log ()};
    }
  off_weekend_box_->setChecked (settings_->value ("LeaveOutOtherGroup", false).toBool ());
  // grids typed in for QSOs logged without one: kept between sessions
  for (auto const& item : settings_->value ("TypedGrids").toStringList ())
    {
      auto const key = item.section ('=', 0, 0), grid = item.section ('=', 1);
      if (!key.isEmpty () && !EmeCabrillo::grid4 (grid).isEmpty ()) fixes_[key] = grid;
    }
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
  settings_->setValue ("Logs", logs_.isEmpty () ? QStringList {QStringLiteral ("<none>")} : logs_);
  settings_->setValue ("LeaveOutOtherGroup", off_weekend_box_->isChecked ());
  QStringList typed;
  for (auto i = fixes_.cbegin (); i != fixes_.cend (); ++i) typed << i.key () + '=' + i.value ();
  typed.sort ();
  settings_->setValue ("TypedGrids", typed);
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

void EmeCabrilloDialog::choose_file ()
{
  auto const start = logs_.size () && logs_.last () != own_log () ? QFileInfo {logs_.last ()}.absolutePath ()
    : QFileInfo {own_log_path_}.absolutePath ();
  auto const path = QFileDialog::getOpenFileName (this, tr ("ADIF log for the Cabrillo entry (only read)"), start,
                                                  tr ("ADIF log (*.adi *.adif);;All files (*)"));
  if (path.isEmpty ()) return;
  add_log (QFileInfo {path} == QFileInfo {own_log_path_} ? own_log () : path);
}

void EmeCabrilloDialog::add_log (QString const& path)
{
  if (path.isEmpty () || logs_.contains (path)) return;
  logs_ << path;
  load ();
}

void EmeCabrilloDialog::remove_log (int index)
{
  if (index < 0 || index >= logs_.size ()) return;
  logs_.removeAt (index);
  load ();
}

// Read every log again (typed grids are kept: they are keyed by call, date
// and time, so they still fit the QSO they were typed for)
void EmeCabrilloDialog::load ()
{
  log_records_.clear ();
  load_errors_.clear ();
  log_state_.clear ();
  for (auto const& entry : logs_)
    {
      auto const path = entry == own_log () ? own_log_path_ : entry;
      QFile file {path};
      QVector<EmeCabrillo::Record> records;
      if (file.open (QIODevice::ReadOnly))
        {
          records = EmeCabrillo::read_adif (file.readAll ());
          log_state_ << 0;
        }
      else if (entry == own_log () && !file.exists ())
        {
          log_state_ << 1;
        }
      else
        {
          log_state_ << 2;
          load_errors_ << tr ("Cannot read %1: %2").arg (QDir::toNativeSeparators (path), file.errorString ());
        }
      log_records_ << records;
    }
  copies_.clear ();
  records_ = EmeCabrillo::merge (log_records_, &copies_);
  count_logs ();
  show_logs ();
  refresh ();
}

// What each log holds on the chosen days (no file is read again)
void EmeCabrilloDialog::count_logs ()
{
  log_info_.clear ();
  for (int i = 0; i < log_records_.size (); ++i)
    {
      if (log_state_.value (i) == 1)
        {
          log_info_ << tr ("no QSOs logged yet");
          continue;
        }
      if (log_state_.value (i) == 2)
        {
          log_info_ << tr ("cannot read it");
          continue;
        }
      int on_days = 0, no_date = 0;
      for (auto const& r : log_records_[i])
        {
          auto const date = QDate::fromString (r.value ("QSO_DATE").trimmed (), "yyyyMMdd");
          if (!date.isValid ()) ++no_date;
          else if (days_.contains (date)) ++on_days;
        }
      auto text = tr ("%1 QSOs, %2 on the days").arg (log_records_[i].size ()).arg (on_days);
      // a record whose QSO_DATE can't be read is never on a chosen day: say so
      if (no_date) text += tr (", %1 WITHOUT A USABLE DATE (never counted)").arg (no_date);
      log_info_ << text;
    }
}

void EmeCabrilloDialog::show_logs ()
{
  while (auto item = logs_layout_->takeAt (0))
    {
      if (item->widget ()) item->widget ()->deleteLater ();
      delete item;
    }
  auto parent = logs_layout_->parentWidget ();
  if (logs_.isEmpty ())
    {
      auto none = new QLabel {tr ("No log chosen - add one."), parent};
      none->setStyleSheet ("color:#c00000");
      logs_layout_->addWidget (none, 0, 0);
    }
  for (int i = 0; i < logs_.size (); ++i)
    {
      auto const own = logs_[i] == own_log ();
      QFileInfo const info {own ? own_log_path_ : logs_[i]};
      auto name = new QLabel {info.fileName () + "   "
                              + (own ? tr ("WSJT-X's log")
                                 : tr ("in %1").arg (QDir::toNativeSeparators (info.absolutePath ()))),
                              parent};
      name->setToolTip (QDir::toNativeSeparators (info.absoluteFilePath ()));
      name->setTextInteractionFlags (Qt::TextSelectableByMouse);
      logs_layout_->addWidget (name, i, 0);
      auto what = new QLabel {log_info_.value (i), parent};
      if (log_state_.value (i) == 2 || what->text ().contains ("WITHOUT")) what->setStyleSheet ("color:#c00000");
      logs_layout_->addWidget (what, i, 1);
      auto remove = new QPushButton {tr ("Remove"), parent};
      remove->setAutoDefault (false);
      remove->setObjectName (QString {"remove_log_%1"}.arg (i));
      connect (remove, &QPushButton::clicked, this, [this, i] {remove_log (i);});
      logs_layout_->addWidget (remove, i, 2);
    }
  logs_layout_->setColumnStretch (0, 1);
  add_own_button_->setEnabled (!logs_.contains (own_log ()));
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
  // modes on those days and bands; those unticked are left out
  auto const with_all_modes = EmeCabrillo::select (records_, days_, my_call_, my_grid_, chosen, fixes_);
  QSet<QString> modes;
  for (auto i = with_all_modes.modes.cbegin (); i != with_all_modes.modes.cend (); ++i)
    if (!modes_off_.contains (i.key ())) modes << i.key ();
  if (modes.isEmpty () && !with_all_modes.modes.isEmpty ()) modes << "-"; // none ticked: nothing
  selection_ = EmeCabrillo::select (records_, days_, my_call_, my_grid_, chosen, fixes_,
                                    off_weekend_box_->isChecked (), modes);
  off_weekend_box_->setVisible (!selection_.off_weekend.isEmpty ());
  // operators named in the logs (several PCs): into an empty box, once
  if (!operators_filled_ && selection_.operators.size () && operators_->text ().trimmed ().isEmpty ())
    {
      operators_filled_ = true;
      operators_->setText (selection_.operators.join (' ')); // refreshes again
      return;
    }
  text_ = EmeCabrillo::cabrillo_text (header (), selection_.qsos);
  show_bands ();
  show_modes ();
  show_grid_rows ();

  QStringList warnings {load_errors_};
  if (logs_.isEmpty ()) warnings << tr ("No log chosen - add one above.");
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
  if (selection_.off_weekend.size ())
    {
      QStringList off;
      for (auto const& qso : selection_.off_weekend) off << qso_text (qso);
      warnings << (off_weekend_box_->isChecked ()
                   ? tr ("%1 QSO(s) on a band not contested that weekend LEFT OUT: %2")
                   : tr ("%1 QSO(s) on a band not contested that weekend KEPT - check them: %2"))
        .arg (selection_.off_weekend.size ()).arg (off.join (", "));
    }
  if (selection_.no_grid.size ())
    {
      QStringList calls;
      for (auto const& qso : selection_.no_grid) calls << qso.call;
      warnings << tr ("%1 QSO(s) left out until you type their grid in above: %2")
        .arg (selection_.no_grid.size ()).arg (calls.join (", "));
    }
  // the header takes My Call and My Grid, each QSO line its own record's
  // STATION_CALLSIGN and MY_GRIDSQUARE: logs from other PCs may differ
  QSet<QString> other_calls, other_grids;
  bool no_own_grid {false};
  for (auto const& qso : selection_.qsos)
    {
      if (qso.my_call != my_call_) other_calls << qso.my_call;
      if (qso.my_grid.isEmpty ()) no_own_grid = true;
      else if (qso.my_grid != EmeCabrillo::grid4 (my_grid_)) other_grids << qso.my_grid;
    }
  auto listed = [] (QSet<QString> const& set) {
    QStringList list;
    for (auto const& item : set) list << item;
    list.sort ();
    return list.join (", ");
  };
  if (other_calls.size () && !my_call_.isEmpty ())
    warnings << tr ("QSOs logged as %1, not My Call %2: the header says %2 - check.")
      .arg (listed (other_calls), my_call_);
  if (other_grids.size () && !EmeCabrillo::grid4 (my_grid_).isEmpty ())
    warnings << tr ("QSOs logged from %1, not My Grid %2: the header says %2 - check.")
      .arg (listed (other_grids), EmeCabrillo::grid4 (my_grid_));
  if (no_own_grid)
    warnings << tr ("QSOs without an own grid (no MY_GRIDSQUARE in the log and no My Grid): cannot be saved.");

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
  // each weekend's band group, as worked out from the logs
  for (auto const& weekend : selection_.weekends)
    {
      QStringList days, counts;
      for (auto const& day : weekend.days) days << day.toString ("dd.MM.");
      for (auto i = weekend.qsos.cbegin (); i != weekend.qsos.cend (); ++i)
        counts << QString {"%1 %2"}.arg (i.key ()).arg (i.value ());
      auto const what = !weekend.group.isEmpty () ? weekend.group
        : weekend.qsos.isEmpty () ? tr ("no QSOs")
        : tr ("both band groups equally - can't tell, all kept");
      auto line = escaped (tr ("Weekend %1 %2: %3").arg (days.join ('/')).arg (weekend.days.first ().year ())
                           .arg (what) + (counts.size () ? "  (" + counts.join (", ") + " " + tr ("QSOs") + ")"
                                          : QString {}));
      html += weekend.group.isEmpty () && !weekend.qsos.isEmpty ()
        ? "<p style='color:#c00000'>" + line + "</p>" : "<p>" + line + "</p>";
    }
  QSet<QString> day_strings;
  for (auto const& day : days_) day_strings << day.toString ("yyyyMMdd");
  int copies = 0;
  for (auto const& record : copies_) copies += day_strings.contains (record.value ("QSO_DATE").trimmed ());
  if (copies)
    html += "<p>" + escaped (tr ("%1 QSO(s) on the days are in more than one log - counted once.").arg (copies))
      + "</p>";
  if (selection_.operators.size ())
    html += "<p>" + escaped (tr ("OPERATOR in the logs: %1").arg (selection_.operators.join (' '))) + "</p>";
  if (selection_.dupes.size ())
    {
      QStringList dupes;
      for (auto const& qso : selection_.dupes) dupes << qso_text (qso);
      html += "<p>" + escaped (tr ("%1 left out as worked before on the band from the same grid: %2")
                               .arg (selection_.dupes.size ()).arg (dupes.join (", "))) + "</p>";
    }
  if (selection_.satellite)
    html += "<p>" + escaped (tr ("%1 satellite QSO(s) left out (not EME).").arg (selection_.satellite)) + "</p>";
  if (selection_.mode_out)
    html += "<p>" + escaped (tr ("%1 QSO(s) in an unticked mode left out.").arg (selection_.mode_out)) + "</p>";
  summary_->setText (html);
  preview_->setPlainText (text_);
  save_button_->setEnabled (selection_.qsos.size () && !my_call_.isEmpty () && !no_own_grid);
}

void EmeCabrilloDialog::show_modes ()
{
  if (modes_shown_ == selection_.modes) return;
  modes_shown_ = selection_.modes;
  while (auto item = modes_layout_->takeAt (0))
    {
      if (item->widget ()) item->widget ()->deleteLater ();
      delete item;
    }
  if (modes_shown_.isEmpty ()) return;
  modes_layout_->addWidget (new QLabel {tr ("Modes"), this});
  for (auto i = modes_shown_.cbegin (); i != modes_shown_.cend (); ++i)
    {
      auto const mode = i.key ();
      auto box = new QCheckBox {QString {"%1 (%2)"}.arg (mode).arg (i.value ()), this};
      box->setObjectName ("mode_" + mode);
      box->setChecked (!modes_off_.contains (mode));
      connect (box, &QCheckBox::toggled, this, [this, mode] (bool on) {
          if (on) modes_off_.remove (mode); else modes_off_ << mode;
          refresh ();
        });
      modes_layout_->addWidget (box);
    }
  auto hint = new QLabel {tr ("untick a mode that wasn't EME"), this};
  hint->setStyleSheet ("color:#666666");
  modes_layout_->addWidget (hint);
  modes_layout_->addStretch ();
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
    if (qso.dupe)                       // that grid makes it a repeat: say so, keep the row
      {
        colour = "#b06000";
        return (qso.entered ? tr ("%1 typed: %2 already counted from there - left out; type the right grid if it was another")
                : tr ("%1 from another QSO: %2 already counted from there - left out; type the right grid if it was another"))
          .arg (qso.grid, qso.call);
      }
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
  // one row per QSO key: the same call logged twice in a minute shares a box
  QStringList keys;
  QVector<EmeCabrillo::Qso> rows;
  for (auto const& qso : selection_.need_grid)
    if (!keys.contains (qso.key))
      {
        keys << qso.key;
        rows << qso;
      }
  if (keys == grid_keys_)
    {
      for (auto const& qso : rows)
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
  for (auto const& qso : rows)
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
    message += "\n\n" + tr ("The %1 grid(s) you typed are in the Cabrillo file only: the ADIF logs are not changed.")
      .arg (typed);
  message += "\n\n" + tr ("Upload it at contests.arrl.org/arrlemescoresubmission.php.");
  MessageBox::information_message (this, message);
}
