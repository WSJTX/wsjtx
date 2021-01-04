#include "ClientWidget.hpp"

#include <limits>
#include <QRegExp>
#include <QColor>
#include <QtWidgets>
#include <QAction>

#include "validators/MaidenheadLocatorValidator.hpp"

namespace
{
  //QRegExp message_alphabet {"[- A-Za-z0-9+./?]*"};
  QRegExp message_alphabet {"[- @A-Za-z0-9+./?#<>]*"};
  QRegularExpression cq_re {"(CQ|CQDX|QRZ)[^A-Z0-9/]+"};
  QRegExpValidator message_validator {message_alphabet};
  MaidenheadLocatorValidator locator_validator;
  quint32 quint32_max {std::numeric_limits<quint32>::max ()};

  void update_dynamic_property (QWidget * widget, char const * property, QVariant const& value)
  {
    widget->setProperty (property, value);
    widget->style ()->unpolish (widget);
    widget->style ()->polish (widget);
    widget->update ();
  }
}

ClientWidget::IdFilterModel::IdFilterModel (ClientKey const& key, QObject * parent)
  : QSortFilterProxyModel {parent}
  , key_ {key}
  , rx_df_ (quint32_max)
{
}

QVariant ClientWidget::IdFilterModel::data (QModelIndex const& proxy_index, int role) const
{
  if (role == Qt::BackgroundRole)
    {
      switch (proxy_index.column ())
        {
        case 8:                 // message
          {
            auto message = QSortFilterProxyModel::data (proxy_index).toString ();
            if (base_call_re_.pattern ().size ()
                && message.contains (base_call_re_))
              {
                return QColor {255,200,200};
              }
            if (message.contains (cq_re))
              {
                return QColor {200, 255, 200};
              }
          }
          break;

        case 4:                 // DF
          if (qAbs (QSortFilterProxyModel::data (proxy_index).toUInt () - rx_df_) <= 10)
            {
              return QColor {255, 200, 200};
            }
          break;

        default:
          break;
        }
    }
  return QSortFilterProxyModel::data (proxy_index, role);
}

bool ClientWidget::IdFilterModel::filterAcceptsRow (int source_row
                                                    , QModelIndex const& source_parent) const
{
  auto source_index_col0 = sourceModel ()->index (source_row, 0, source_parent);
  return sourceModel ()->data (source_index_col0, Qt::UserRole + 1).value<ClientKey> () == key_;
}

void ClientWidget::IdFilterModel::de_call (QString const& call)
{
  if (call != call_)
    {
      beginResetModel ();
      if (call.size ())
        {
          base_call_re_.setPattern ("[^A-Z0-9]*" + Radio::base_callsign (call) + "[^A-Z0-9]*");
        }
      else
        {
          base_call_re_.setPattern (QString {});
        }
      call_ = call;
      endResetModel ();
    }
}

void ClientWidget::IdFilterModel::rx_df (quint32 df)
{
  if (df != rx_df_)
    {
      beginResetModel ();
      rx_df_ = df;
      endResetModel ();
    }
}

namespace
{
  QString make_title (MessageServer::ClientKey const& key, QString const& version, QString const& revision)
  {
    QString title {QString {"%1(%2)"}.arg (key.second).arg (key.first.toString ())};
    if (version.size ())
      {
        title += QString {" v%1"}.arg (version);
      }
    if (revision.size ())
      {
        title += QString {" (%1)"}.arg (revision);
      }
    return title;
  }
}

ClientWidget::ClientWidget (QAbstractItemModel * decodes_model, QAbstractItemModel * beacons_model
                            , ClientKey const& key, QString const& version, QString const& revision
                            , QListWidget const * calls_of_interest, QWidget * parent)
  : QDockWidget {make_title (key, version, revision), parent}
  , key_ {key}
  , done_ {false}
  , calls_of_interest_ {calls_of_interest}
  , decodes_proxy_model_ {key}
  , beacons_proxy_model_ {key}
  , erase_action_ {new QAction {tr ("&Erase Band Activity"), this}}
  , erase_rx_frequency_action_ {new QAction {tr ("Erase &Rx Frequency"), this}}
  , erase_both_action_ {new QAction {tr ("Erase &Both"), this}}
  , decodes_table_view_ {new QTableView {this}}
  , beacons_table_view_ {new QTableView {this}}
  , message_line_edit_ {new QLineEdit {this}}
  , grid_line_edit_ {new QLineEdit {this}}
  , generate_messages_push_button_ {new QPushButton {tr ("&Gen Msgs"), this}}
  , auto_off_button_ {nullptr}
  , halt_tx_button_ {nullptr}
  , de_label_ {new QLabel {this}}
  , frequency_label_ {new QLabel {this}}
  , tx_df_label_ {new QLabel {this}}
  , report_label_ {new QLabel {this}}
  , configuration_line_edit_ {new QLineEdit {this}}
  , mode_line_edit_ {new QLineEdit {this}}
  , frequency_tolerance_spin_box_ {new QSpinBox {this}}
  , tx_mode_label_ {new QLabel {this}}
  , tx_message_label_ {new QLabel {this}}
  , submode_line_edit_ {new QLineEdit {this}}
  , fast_mode_check_box_ {new QCheckBox {this}}
  , tr_period_spin_box_ {new QSpinBox {this}}
  , rx_df_spin_box_ {new QSpinBox {this}}
  , dx_call_line_edit_ {new QLineEdit {this}}
  , dx_grid_line_edit_ {new QLineEdit {this}}
  , decodes_page_ {new QWidget {this}}
  , beacons_page_ {new QWidget {this}}
  , content_widget_ {new QFrame {this}}
  , status_bar_ {new QStatusBar {this}}
  , control_button_box_ {new QDialogButtonBox {this}}
  , form_layout_ {new QFormLayout}
  , horizontal_layout_ {new QHBoxLayout}
  , subform1_layout_ {new QFormLayout}
  , subform2_layout_ {new QFormLayout}
  , subform3_layout_ {new QFormLayout}
  , decodes_layout_ {new QVBoxLayout {decodes_page_}}
  , beacons_layout_ {new QVBoxLayout {beacons_page_}}
  , content_layout_ {new QVBoxLayout {content_widget_}}
  , decodes_stack_ {new QStackedLayout}
  , columns_resized_ {false}
{
  // set up widgets
  decodes_proxy_model_.setSourceModel (decodes_model);
  decodes_table_view_->setModel (&decodes_proxy_model_);
  decodes_table_view_->verticalHeader ()->hide ();
  decodes_table_view_->hideColumn (0);
  decodes_table_view_->horizontalHeader ()->setStretchLastSection (true);
  decodes_table_view_->setContextMenuPolicy (Qt::ActionsContextMenu);
  decodes_table_view_->insertAction (nullptr, erase_action_);
  decodes_table_view_->insertAction (nullptr, erase_rx_frequency_action_);
  decodes_table_view_->insertAction (nullptr, erase_both_action_);

  message_line_edit_->setValidator (&message_validator);
  grid_line_edit_->setValidator (&locator_validator);
  dx_grid_line_edit_->setValidator (&locator_validator);
  tr_period_spin_box_->setRange (5, 1800);
  tr_period_spin_box_->setSuffix (" s");
  rx_df_spin_box_->setRange (200, 5000);
  frequency_tolerance_spin_box_->setRange (1, 1000);
  frequency_tolerance_spin_box_->setPrefix ("\u00b1");
  frequency_tolerance_spin_box_->setSuffix (" Hz");

  form_layout_->addRow (tr ("Free text:"), message_line_edit_);
  form_layout_->addRow (tr ("Temporary grid:"), grid_line_edit_);
  form_layout_->addRow (tr ("Configuration name:"), configuration_line_edit_);
  form_layout_->addRow (horizontal_layout_);
  subform1_layout_->addRow (tr ("Mode:"), mode_line_edit_);
  subform2_layout_->addRow (tr ("Submode:"), submode_line_edit_);
  subform3_layout_->addRow (tr ("Fast mode:"), fast_mode_check_box_);
  subform1_layout_->addRow (tr ("T/R period:"), tr_period_spin_box_);
  subform2_layout_->addRow (tr ("Rx DF:"), rx_df_spin_box_);
  subform3_layout_->addRow (tr ("Freq. Tol:"), frequency_tolerance_spin_box_);
  subform1_layout_->addRow (tr ("DX call:"), dx_call_line_edit_);
  subform2_layout_->addRow (tr ("DX grid:"), dx_grid_line_edit_);
  subform3_layout_->addRow (generate_messages_push_button_);
  horizontal_layout_->addLayout (subform1_layout_);
  horizontal_layout_->addLayout (subform2_layout_);
  horizontal_layout_->addLayout (subform3_layout_);

  connect (message_line_edit_, &QLineEdit::textEdited, [this] (QString const& text) {
      Q_EMIT do_free_text (key_, text, false);
    });
  connect (message_line_edit_, &QLineEdit::editingFinished, [this] () {
      Q_EMIT do_free_text (key_, message_line_edit_->text (), true);
    });
  connect (grid_line_edit_, &QLineEdit::editingFinished, [this] () {
      Q_EMIT location (key_, grid_line_edit_->text ());
  });
  connect (configuration_line_edit_, &QLineEdit::editingFinished, [this] () {
      Q_EMIT switch_configuration (key_, configuration_line_edit_->text ());
  });
  connect (mode_line_edit_, &QLineEdit::editingFinished, [this] () {
      QString empty;
      Q_EMIT configure (key_, mode_line_edit_->text (), quint32_max, empty, fast_mode ()
                        , quint32_max, quint32_max, empty, empty, false);
  });
  connect (frequency_tolerance_spin_box_, static_cast<void (QSpinBox::*) (int)> (&QSpinBox::valueChanged), [this] (int i) {
      QString empty;
      auto f = frequency_tolerance_spin_box_->specialValueText ().size () ? quint32_max : i;
      Q_EMIT configure (key_, empty, f, empty, fast_mode ()
                        , quint32_max, quint32_max, empty, empty, false);
  });
  connect (submode_line_edit_, &QLineEdit::editingFinished, [this] () {
      QString empty;
      Q_EMIT configure (key_, empty, quint32_max, submode_line_edit_->text (), fast_mode ()
                        , quint32_max, quint32_max, empty, empty, false);
  });
  connect (fast_mode_check_box_, &QCheckBox::stateChanged, [this] (int state) {
      QString empty;
      Q_EMIT configure (key_, empty, quint32_max, empty, Qt::Checked == state
                        , quint32_max, quint32_max, empty, empty, false);
  });
  connect (tr_period_spin_box_, static_cast<void (QSpinBox::*) (int)> (&QSpinBox::valueChanged), [this] (int i) {
      QString empty;
      Q_EMIT configure (key_, empty, quint32_max, empty, fast_mode ()
                        , i, quint32_max, empty, empty, false);
  });
  connect (rx_df_spin_box_, static_cast<void (QSpinBox::*) (int)> (&QSpinBox::valueChanged), [this] (int i) {
      QString empty;
      Q_EMIT configure (key_, empty, quint32_max, empty, fast_mode ()
                        , quint32_max, i, empty, empty, false);
  });
  connect (dx_call_line_edit_, &QLineEdit::editingFinished, [this] () {
      QString empty;
      Q_EMIT configure (key_, empty, quint32_max, empty, fast_mode ()
                        , quint32_max, quint32_max, dx_call_line_edit_->text (), empty, false);
  });
  connect (dx_grid_line_edit_, &QLineEdit::editingFinished, [this] () {
      QString empty;
      Q_EMIT configure (key_, empty, quint32_max, empty, fast_mode ()
                        , quint32_max, quint32_max, empty, dx_grid_line_edit_->text (), false);
  });

  decodes_layout_->setContentsMargins (QMargins {2, 2, 2, 2});
  decodes_layout_->addWidget (decodes_table_view_);
  decodes_layout_->addLayout (form_layout_);

  beacons_proxy_model_.setSourceModel (beacons_model);
  beacons_table_view_->setModel (&beacons_proxy_model_);
  beacons_table_view_->verticalHeader ()->hide ();
  beacons_table_view_->hideColumn (0);
  beacons_table_view_->horizontalHeader ()->setStretchLastSection (true);
  beacons_table_view_->setContextMenuPolicy (Qt::ActionsContextMenu);
  beacons_table_view_->insertAction (nullptr, erase_action_);

  beacons_layout_->setContentsMargins (QMargins {2, 2, 2, 2});
  beacons_layout_->addWidget (beacons_table_view_);

  decodes_stack_->addWidget (decodes_page_);
  decodes_stack_->addWidget (beacons_page_);

  // stack alternative views
  content_layout_->setContentsMargins (QMargins {2, 2, 2, 2});
  content_layout_->addLayout (decodes_stack_);

  // set up controls
  auto_off_button_ = control_button_box_->addButton (tr ("&Auto Off"), QDialogButtonBox::ActionRole);
  halt_tx_button_ = control_button_box_->addButton (tr ("&Halt Tx"), QDialogButtonBox::ActionRole);
  connect (generate_messages_push_button_, &QAbstractButton::clicked, [this] (bool /*checked*/) {
      QString empty;
      Q_EMIT configure (key_, empty, quint32_max, empty, fast_mode ()
                        , quint32_max, quint32_max, empty, empty, true);
  });
  connect (auto_off_button_, &QAbstractButton::clicked, [this] (bool /* checked */) {
      Q_EMIT do_halt_tx (key_, true);
    });
  connect (halt_tx_button_, &QAbstractButton::clicked, [this] (bool /* checked */) {
      Q_EMIT do_halt_tx (key_, false);
    });
  content_layout_->addWidget (control_button_box_);

  // set up status area
  status_bar_->addPermanentWidget (de_label_);
  status_bar_->addPermanentWidget (tx_mode_label_);
  status_bar_->addPermanentWidget (tx_message_label_);
  status_bar_->addPermanentWidget (frequency_label_);
  status_bar_->addPermanentWidget (tx_df_label_);
  status_bar_->addPermanentWidget (report_label_);
  content_layout_->addWidget (status_bar_);
  connect (this, &ClientWidget::topLevelChanged, status_bar_, &QStatusBar::setSizeGripEnabled);

  // set up central widget
  content_widget_->setFrameStyle (QFrame::StyledPanel | QFrame::Sunken);
  setWidget (content_widget_);
  // setMinimumSize (QSize {550, 0});
  setAllowedAreas (Qt::BottomDockWidgetArea);
  setFloating (true);

  // connect context menu actions
  connect (erase_action_, &QAction::triggered, [this] (bool /*checked*/) {
      Q_EMIT do_clear_decodes (key_);
    });
  connect (erase_rx_frequency_action_, &QAction::triggered, [this] (bool /*checked*/) {
      Q_EMIT do_clear_decodes (key_, 1);
    });
  connect (erase_both_action_, &QAction::triggered, [this] (bool /*checked*/) {
      Q_EMIT do_clear_decodes (key_, 2);
    });

  // connect up table view signals
  connect (decodes_table_view_, &QTableView::doubleClicked, [this] (QModelIndex const& index) {
      Q_EMIT do_reply (decodes_proxy_model_.mapToSource (index), QApplication::keyboardModifiers () >> 24);
    });

  // tell new client about calls of interest
  for (int row = 0; row < calls_of_interest_->count (); ++row)
    {
      Q_EMIT highlight_callsign (key_, calls_of_interest_->item (row)->text (), QColor {Qt::blue}, QColor {Qt::yellow});
    }
}

void ClientWidget::dispose ()
{
  done_ = true;
  close ();
}

void ClientWidget::closeEvent (QCloseEvent *e)
{
  if (!done_)
    {
      Q_EMIT do_close (key_);
      e->ignore ();      // defer closure until client actually closes
    }
  else
    {
      QDockWidget::closeEvent (e);
    }
}

ClientWidget::~ClientWidget ()
{
  for (int row = 0; row < calls_of_interest_->count (); ++row)
    {
      // tell client to forget calls of interest
      Q_EMIT highlight_callsign (key_, calls_of_interest_->item (row)->text ());
    }
}

bool ClientWidget::fast_mode () const
{
  return fast_mode_check_box_->isChecked ();
}

namespace
{
  void update_line_edit (QLineEdit * le, QString const& value, bool allow_empty = true)
  {
    le->setEnabled (value.size () || allow_empty);
    if (!(le->hasFocus () && le->isModified ()))
      {
        le->setText (value);
      }
  }

  void update_spin_box (QSpinBox * sb, int value, QString const& special_value = QString {})
  {
    sb->setSpecialValueText (special_value);
    bool enable {0 == special_value.size ()};
    sb->setEnabled (enable);
    if (!sb->hasFocus () && enable)
      {
        sb->setValue (value);
      }
  }
}

void ClientWidget::update_status (ClientKey const& key, Frequency f, QString const& mode, QString const& dx_call
                                  , QString const& report, QString const& tx_mode, bool tx_enabled
                                  , bool transmitting, bool decoding, quint32 rx_df, quint32 tx_df
                                  , QString const& de_call, QString const& de_grid, QString const& dx_grid
                                  , bool watchdog_timeout, QString const& submode, bool fast_mode
                                  , quint8 special_op_mode, quint32 frequency_tolerance, quint32 tr_period
                                  , QString const& configuration_name, QString const& tx_message)
{
    if (key == key_)
    {
      fast_mode_check_box_->setChecked (fast_mode);
      decodes_proxy_model_.de_call (de_call);
      decodes_proxy_model_.rx_df (rx_df);
      QString special;
      switch (special_op_mode)
        {
        case 1: special = "[NA VHF]"; break;
        case 2: special = "[EU VHF]"; break;
        case 3: special = "[FD]"; break;
        case 4: special = "[RTTY RU]"; break;
        case 5: special = "[WW DIGI]"; break;
        case 6: special = "[Fox]"; break;
        case 7: special = "[Hound]"; break;
        default: break;
        }
      de_label_->setText (de_call.size () >= 0 ? QString {"DE: %1%2%3"}.arg (de_call)
                          .arg (de_grid.size () ? '(' + de_grid + ')' : QString {})
                          .arg (special)
                          : QString {});
      update_line_edit (mode_line_edit_, mode);
      update_spin_box (frequency_tolerance_spin_box_, frequency_tolerance
                       , quint32_max == frequency_tolerance ? QString {"n/a"} : QString {});
      update_line_edit (submode_line_edit_, submode, false);
      tx_mode_label_->setText (tx_mode.isEmpty () || tx_mode == mode ? "" : "Tx Mode: (" + tx_mode + ')');
      tx_message_label_->setText (tx_message.isEmpty () ? "" : "Tx Msg: " + tx_message.trimmed ());
      frequency_label_->setText ("QRG: " + Radio::pretty_frequency_MHz_string (f));
      update_line_edit (dx_call_line_edit_, dx_call);
      update_line_edit (dx_grid_line_edit_, dx_grid);
      if (rx_df != quint32_max) update_spin_box (rx_df_spin_box_, rx_df);
      update_spin_box (tr_period_spin_box_, tr_period
                       , quint32_max == tr_period ? QString {"n/a"} : QString {});
      tx_df_label_->setText (QString {"Tx: %1"}.arg (tx_df));
      report_label_->setText ("SNR: " + report);
      update_dynamic_property (frequency_label_, "transmitting", transmitting);
      auto_off_button_->setEnabled (tx_enabled);
      halt_tx_button_->setEnabled (transmitting);
      update_line_edit (configuration_line_edit_, configuration_name);
      update_dynamic_property (mode_line_edit_, "decoding", decoding);
      update_dynamic_property (tx_df_label_, "watchdog_timeout", watchdog_timeout);
    }
}

void ClientWidget::decode_added (bool /*is_new*/, ClientKey const& key, QTime /*time*/, qint32 /*snr*/
                                 , float /*delta_time*/, quint32 /*delta_frequency*/, QString const& /*mode*/
                                 , QString const& /*message*/, bool /*low_confidence*/, bool /*off_air*/)
{
  if (key == key_ && !columns_resized_)
    {
      decodes_stack_->setCurrentIndex (0);
      decodes_table_view_->resizeColumnsToContents ();
      columns_resized_ = true;
    }
  decodes_table_view_->scrollToBottom ();
}

void ClientWidget::beacon_spot_added (bool /*is_new*/, ClientKey const& key, QTime /*time*/, qint32 /*snr*/
                                      , float /*delta_time*/, Frequency /*delta_frequency*/, qint32 /*drift*/
                                      , QString const& /*callsign*/, QString const& /*grid*/, qint32 /*power*/
                                      , bool /*off_air*/)
{
  if (key == key_ && !columns_resized_)
    {
      decodes_stack_->setCurrentIndex (1);
      beacons_table_view_->resizeColumnsToContents ();
      columns_resized_ = true;
    }
  beacons_table_view_->scrollToBottom ();
}

void ClientWidget::decodes_cleared (ClientKey const& key)
{
  if (key == key_)
    {
      columns_resized_ = false;
    }
}

#include "moc_ClientWidget.cpp"
