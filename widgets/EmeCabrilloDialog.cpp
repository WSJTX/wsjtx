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
#include <QSaveFile>
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
  auto logs_box = new QGroupBox {tr ("Logs"), this};
  logs_box->setToolTip (tr ("The ADIF logs the entry is made from. Several are merged into one entry, and a "
                            "QSO found in two of them counts once. Every log is only read."));
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
  logs_buttons->addWidget (new QLabel {tr ("Only read."), logs_box}, 1);
  logs_box_layout->addLayout (logs_buttons);
  layout->addWidget (logs_box);

  // contest days (the dialog's own, saved; not Active Stations') and bands
  auto days_row = new QHBoxLayout;
  days_row->addWidget (new QLabel {tr ("Contest days"), this});
  days_button_ = new QToolButton {this};
  connect (days_button_, &QToolButton::clicked, this, [this] {
      DaysCalendar::show (days_button_, &days_, [this] {
          Q_EMIT days_changed (days_);
          count_logs ();                // each log's "on the days" count
          show_logs ();
          refresh ();
        }, tr ("No days chosen: nothing to export."));
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
  off_weekend_box_ = new QCheckBox {tr ("Leave out QSOs made on the other band group's weekend"), this};
  off_weekend_box_->setToolTip (tr ("50-1296 MHz and 2.3 GHz and up have their own contest weekends. A weekend's "
                                    "group is taken to be the one with more QSOs on it."));
  off_weekend_box_->setVisible (false);
  layout->addWidget (off_weekend_box_);

  // QSOs logged without a grid: all listed, to type them in at once
  grids_box_ = new QGroupBox {tr ("Missing grids"), this};
  grids_box_->setToolTip (tr ("QSOs logged without a grid, or whose grid needs checking. Type the grid and press "
                              "Enter or Tab. The grids you type are remembered; the ADIF logs are not changed."));
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
  auto hint = new QLabel {tr ("Upload at contests.arrl.org/arrlemescoresubmission.php"), this};
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
  // the entry's details change only the header of the file, not which QSOs
  // are in it: the preview is made again, the logs are not gone through
  for (auto edit : {operators_, location_, name_, address_, city_, state_, postcode_, country_, email_})
    connect (edit, &QLineEdit::textChanged, this, &EmeCabrilloDialog::update_text);
  connect (single_op_, &QRadioButton::toggled, this, &EmeCabrilloDialog::update_text);
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
  // "Days": the dialog's own contest days, kept apart from Active Stations'
  // "on days" so that log work never changes what is used while operating;
  // first time: the days the dialog was opened with
  if (settings_->contains ("Days"))
    {
      days_.clear ();
      for (auto const& text : settings_->value ("Days").toStringList ())
        {
          auto const day = QDate::fromString (text, Qt::ISODate);
          if (day.isValid ()) days_.insert (day);
        }
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
  QStringList days;
  for (auto const& day : DaysCalendar::sorted (days_)) days << day.toString (Qt::ISODate);
  settings_->setValue ("Days", days);
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
  // Made ready once, here (sorted, dates read, known grids collected): a
  // change of days, bands, modes or grids then only goes through the QSOs
  // on the chosen days.
  log_ = EmeCabrillo::prepare (EmeCabrillo::merge (log_records_, &copies_));
  log_dates_.clear ();
  for (auto const& records : log_records_)
    {
      QVector<QDate> dates;
      dates.reserve (records.size ());
      for (auto const& r : records) dates << EmeCabrillo::record_date (r);
      log_dates_ << dates;
    }
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
      for (auto const& date : log_dates_.value (i))
        {
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

// The header changed (name, address, operators ...): the same QSOs, a new text
void EmeCabrilloDialog::update_text ()
{
  text_ = EmeCabrillo::cabrillo_text (header (), selection_.qsos);
  preview_->setPlainText (text_);
}

void EmeCabrilloDialog::refresh ()
{
  auto const n = days_.size ();
  days_button_->setText (n == 0 ? tr ("choose…") : n == 1 ? tr ("1 day") : tr ("%1 days").arg (n));
  days_label_->setText (DaysCalendar::text (days_, tr ("(choose the contest days)")));

  // bands with QSOs on the days; those unticked are left out
  auto const all = EmeCabrillo::select (log_, days_, my_call_, my_grid_, {}, fixes_);
  QSet<QString> chosen;
  for (auto const& band : all.bands)
    if (!bands_off_.contains (band)) chosen << band;
  if (chosen.isEmpty () && !all.bands.isEmpty ()) chosen << "-"; // none ticked: nothing
  // modes on those days and bands; those unticked are left out
  auto const with_all_modes = EmeCabrillo::select (log_, days_, my_call_, my_grid_, chosen, fixes_);
  QSet<QString> modes;
  for (auto i = with_all_modes.modes.cbegin (); i != with_all_modes.modes.cend (); ++i)
    if (!modes_off_.contains (i.key ())) modes << i.key ();
  if (modes.isEmpty () && !with_all_modes.modes.isEmpty ()) modes << "-"; // none ticked: nothing
  selection_ = EmeCabrillo::select (log_, days_, my_call_, my_grid_, chosen, fixes_,
                                    off_weekend_box_->isChecked (), modes);
  off_weekend_box_->setVisible (!selection_.off_weekend.isEmpty ());
  // operators named in the logs (several PCs): into an empty box, once
  if (!operators_filled_ && selection_.operators.size () && operators_->text ().trimmed ().isEmpty ())
    {
      operators_filled_ = true;
      operators_->setText (selection_.operators.join (' '));
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
    warnings << tr ("Not a Saturday or Sunday: %1.")
      .arg (weekdays.join (", "));
  if (days_.size () && selection_.qsos.isEmpty ()) warnings << tr ("No EME QSOs on these days, bands and modes.");
  if (selection_.off_weekend.size ())
    {
      QStringList off;
      for (auto const& qso : selection_.off_weekend) off << qso_text (qso);
      warnings << (off_weekend_box_->isChecked ()
                   ? tr ("%1 QSO(s) on the other band group's weekend left out: %2")
                   : tr ("%1 QSO(s) on the other band group's weekend - check: %2"))
        .arg (selection_.off_weekend.size ())
        .arg (off.mid (0, 6).join (", ") + (off.size () > 6 ? QString {" ..."} : QString {}));
    }
  if (selection_.no_grid.size ())
    {
      QStringList calls;
      for (auto const& qso : selection_.no_grid) calls << qso.call;
      warnings << tr ("%1 QSO(s) need a grid: %2")
        .arg (selection_.no_grid.size ()).arg (calls.join (", "));
    }
  if (selection_.no_time.size ())
    warnings << tr ("%1 QSO(s) without a time left out: %2")
      .arg (selection_.no_time.size ()).arg (selection_.no_time.mid (0, 12).join (", "));
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
    warnings << tr ("Logged as %1, not My Call %2 - check.")
      .arg (listed (other_calls), my_call_);
  if (other_grids.size () && !EmeCabrillo::grid4 (my_grid_).isEmpty ())
    warnings << tr ("Logged from %1, not My Grid %2 - check.")
      .arg (listed (other_grids), EmeCabrillo::grid4 (my_grid_));
  if (no_own_grid)
    warnings << tr ("Some QSOs have no own grid: cannot be saved.");

  QString html;
  for (auto const& w : warnings) html += "<p style='color:#c00000'>" + escaped (w) + "</p>";
  if (selection_.qsos.size ())
    {
      auto const s = EmeCabrillo::score (selection_.qsos);
      // the bands across, so that many bands do not push the file out of view
      QString bands_row, qsos_row, grids_row;
      for (auto const& b : s.bands)
        {
          bands_row += QString {"<th align='right'>&nbsp;&nbsp;%1</th>"}.arg (b.band);
          qsos_row += QString {"<td align='right'>%1</td>"}.arg (b.qsos);
          grids_row += QString {"<td align='right'>%1</td>"}.arg (b.grids);
        }
      html += "<table cellspacing='0' cellpadding='1'><tr><th align='left'>" + tr ("Band") + "</th>" + bands_row
        + "<th align='right'>&nbsp;&nbsp;" + tr ("Total") + "</th></tr><tr><td>" + tr ("QSOs") + "</td>" + qsos_row
        + QString {"<td align='right'><b>%1</b></td></tr>"}.arg (selection_.qsos.size ())
        + "<tr><td>" + tr ("Grids") + "</td>" + grids_row
        + QString {"<td align='right'><b>%1</b></td></tr></table>"}.arg (s.multipliers)
        + "<p><b>" + escaped (tr ("Claimed score %1 x %2 = %3").arg (s.points).arg (s.multipliers).arg (s.claimed))
        + "</b></p>";
    }
  // Below the score: one line for what was left out, the weekends only
  // when they matter; the details (which dupes, the counts) in the tooltip.
  QStringList details;
  bool weekends_matter = !selection_.off_weekend.isEmpty ();
  QStringList weekend_texts;
  for (auto const& weekend : selection_.weekends)
    {
      QStringList days, counts;
      for (auto const& day : weekend.days) days << QLocale {QLocale::English}.toString (day, "d MMM");
      for (auto i = weekend.qsos.cbegin (); i != weekend.qsos.cend (); ++i)
        counts << QString {"%1 %2"}.arg (i.key ()).arg (i.value ());
      auto const tie = weekend.group.isEmpty () && !weekend.qsos.isEmpty ();
      weekends_matter |= tie;
      weekend_texts << days.join ('/') + ": " + (tie ? tr ("both groups equally") : weekend.group.isEmpty ()
                                                 ? tr ("no QSOs") : weekend.group);
      details << tr ("Weekend %1: %2").arg (days.join ('/'), counts.isEmpty () ? tr ("no QSOs") : counts.join (", "));
    }
  if (weekends_matter) html += "<p>" + escaped (tr ("Weekends: %1").arg (weekend_texts.join ("; "))) + "</p>";
  int copies = 0;
  for (auto const& record : copies_) copies += days_.contains (EmeCabrillo::record_date (record));
  QStringList left_out;
  if (selection_.dupes.size ()) left_out << tr ("%1 dupes").arg (selection_.dupes.size ());
  if (copies) left_out << tr ("%1 in two logs").arg (copies);
  if (selection_.satellite) left_out << tr ("%1 satellite").arg (selection_.satellite);
  if (selection_.not_eme) left_out << tr ("%1 not EME (PROP_MODE)").arg (selection_.not_eme);
  if (selection_.mode_out) left_out << tr ("%1 in unticked modes").arg (selection_.mode_out);
  if (left_out.size ()) html += "<p>" + escaped (tr ("Left out: %1.").arg (left_out.join (", "))) + "</p>";
  if (selection_.dupes.size ())
    {
      QStringList dupes;
      for (auto const& qso : selection_.dupes) dupes << qso_text (qso);
      details << tr ("Dupes (worked before on the band from the same grid): %1").arg (dupes.join (", "));
    }
  // the operators the logs name, if the Operators box does not have them all
  if (multi_op_->isChecked ())
    {
      QStringList missing;
      auto const listed_operators = operators_->text ().toUpper ().split (QRegularExpression {"[\\s,]+"}, Qt::SkipEmptyParts);
      for (auto const& op : selection_.operators) if (!listed_operators.contains (op)) missing << op;
      if (missing.size ())
        html += "<p>" + escaped (tr ("Also in the logs as operator: %1").arg (missing.join (' '))) + "</p>";
    }
  summary_->setToolTip (details.join ("\n"));
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
  auto hint = new QLabel {tr ("untick any that weren't EME"), this};
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
    if (!qso.grid2.isEmpty () && !qso.entered)  // the logs disagree: the user settles it
      {
        colour = "#c00000";
        return tr ("logs disagree: %1 or %2").arg (qso.grid, qso.grid2);
      }
    if (qso.dupe)                       // that grid makes it a repeat: say so, keep the row
      {
        colour = "#b06000";
        return tr ("%1: already counted - retype if it was another grid").arg (qso.grid);
      }
    if (qso.entered)
      {
        colour = "#008000";
        return tr ("entered");
      }
    if (qso.filled)
      {
        colour = "#b06000";
        return tr ("%1 from another QSO - check").arg (qso.grid);
      }
    colour = "#c00000";
    return tr ("needed");
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
  // Never onto one of the logs the entry is made from, also not through a
  // link to it: they are only read, and the log would be lost.
  QFileInfo const target {path};
  for (auto const& entry : logs_)
    {
      QFileInfo const log {entry == own_log () ? own_log_path_ : entry};
      auto const same = target.exists () && log.exists ()
        ? target.canonicalFilePath () == log.canonicalFilePath ()
        : QDir::cleanPath (target.absoluteFilePath ()) == QDir::cleanPath (log.absoluteFilePath ());
      if (same)
        {
          MessageBox::warning_message (this, tr ("Cabrillo log"),
                                       tr ("\"%1\" is one of the logs the entry is made from. Saving the Cabrillo "
                                           "file there would destroy that log: choose another name.")
                                       .arg (QDir::toNativeSeparators (path)));
          return false;
        }
    }
  // Written to a temporary file beside it and put in place only when all of
  // it is on disk: a full disk leaves no cut-off entry, and an earlier
  // export of that name stays as it was.
  QSaveFile file {path};
  auto const bytes = text_.toLatin1 ();
  if (!file.open (QIODevice::WriteOnly | QIODevice::Text)
      || file.write (bytes) != bytes.size ()
      || !file.commit ())
    {
      MessageBox::warning_message (this, tr ("Cabrillo log"), tr ("Cannot write \"%1\": %2")
                                   .arg (QDir::toNativeSeparators (path), file.errorString ()));
      return false;
    }
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
