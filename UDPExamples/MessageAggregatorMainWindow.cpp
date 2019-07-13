#include "MessageAggregatorMainWindow.hpp"

#include <QtWidgets>
#include <QDateTime>

#include "DecodesModel.hpp"
#include "BeaconsModel.hpp"
#include "ClientWidget.hpp"

using port_type = MessageServer::port_type;

namespace
{
  char const * const headings[] = {
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Time On"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Time Off"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Callsign"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Grid"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Name"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Frequency"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Mode"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Sent"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Rec'd"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Power"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Operator"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "My Call"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "My Grid"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Exchange Sent"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Exchange Rcvd"),
    QT_TRANSLATE_NOOP ("MessageAggregatorMainWindow", "Comments"),
  };
}

MessageAggregatorMainWindow::MessageAggregatorMainWindow ()
  : log_ {new QStandardItemModel {0, sizeof headings / sizeof headings[0], this}}
  , decodes_model_ {new DecodesModel {this}}
  , beacons_model_ {new BeaconsModel {this}}
  , server_ {new MessageServer {this}}
  , multicast_group_line_edit_ {new QLineEdit}
  , log_table_view_ {new QTableView}
  , add_call_of_interest_action_ {new QAction {tr ("&Add callsign"), this}}
  , delete_call_of_interest_action_ {new QAction {tr ("&Delete callsign"), this}}
  , last_call_of_interest_action_ {new QAction {tr ("&Highlight last only"), this}}
  , call_of_interest_bg_colour_action_ {new QAction {tr ("&Background colour"), this}}
  , call_of_interest_fg_colour_action_ {new QAction {tr ("&Foreground colour"), this}}
{
  // logbook
  int column {0};
  for (auto const& heading : headings)
    {
      log_->setHeaderData (column++, Qt::Horizontal, tr (heading));
    }
  connect (server_, &MessageServer::qso_logged, this, &MessageAggregatorMainWindow::log_qso);

  // menu bar
  auto file_menu = menuBar ()->addMenu (tr ("&File"));

  auto exit_action = new QAction {tr ("E&xit"), this};
  exit_action->setShortcuts (QKeySequence::Quit);
  exit_action->setToolTip (tr ("Exit the application"));
  file_menu->addAction (exit_action);
  connect (exit_action, &QAction::triggered, this, &MessageAggregatorMainWindow::close);

  view_menu_ = menuBar ()->addMenu (tr ("&View"));

  // central layout
  auto central_layout = new QVBoxLayout;

  // server details
  auto port_spin_box = new QSpinBox;
  port_spin_box->setMinimum (1);
  port_spin_box->setMaximum (std::numeric_limits<port_type>::max ());
  auto group_box_layout = new QFormLayout;
  group_box_layout->addRow (tr ("Port number:"), port_spin_box);
  group_box_layout->addRow (tr ("Multicast Group (blank for unicast server):"), multicast_group_line_edit_);
  auto group_box = new QGroupBox {tr ("Server Details")};
  group_box->setLayout (group_box_layout);
  central_layout->addWidget (group_box);

  log_table_view_->setModel (log_);
  log_table_view_->verticalHeader ()->hide ();
  central_layout->addWidget (log_table_view_);

  // central widget
  auto central_widget = new QWidget;
  central_widget->setLayout (central_layout);

  // main window setup
  setCentralWidget (central_widget);
  setDockOptions (AnimatedDocks | AllowNestedDocks | AllowTabbedDocks);
  setTabPosition (Qt::BottomDockWidgetArea, QTabWidget::North);

  QDockWidget * calls_dock {new QDockWidget {tr ("Calls of Interest"), this}};
  calls_dock->setAllowedAreas (Qt::RightDockWidgetArea);
  calls_of_interest_ = new QListWidget {calls_dock};
  calls_of_interest_->setContextMenuPolicy (Qt::ActionsContextMenu);
  calls_of_interest_->insertAction (nullptr, add_call_of_interest_action_);
  connect (add_call_of_interest_action_, &QAction::triggered, [this] () {
      auto item = new QListWidgetItem {};
      item->setFlags (item->flags () | Qt::ItemIsEditable);
      item->setData (Qt::UserRole, QString {});
      calls_of_interest_->addItem (item);
      calls_of_interest_->editItem (item);
    });
  calls_of_interest_->insertAction (nullptr, delete_call_of_interest_action_);
  connect (delete_call_of_interest_action_, &QAction::triggered, [this] () {
      for (auto item : calls_of_interest_->selectedItems ())
        {
          auto old_call = item->data (Qt::UserRole);
          if (old_call.isValid ()) change_highlighting (old_call.toString ());
          delete item;
        }
    });
  calls_of_interest_->insertAction (nullptr, last_call_of_interest_action_);
  connect (last_call_of_interest_action_, &QAction::triggered, [this] () {
      for (auto item : calls_of_interest_->selectedItems ())
        {
          auto old_call = item->data (Qt::UserRole);
          change_highlighting (old_call.toString ());
          change_highlighting (old_call.toString ()
                               , item->background ().color (), item->foreground ().color (), true);
          delete item;
        }
    });
  calls_of_interest_->insertAction (nullptr, call_of_interest_bg_colour_action_);
  connect (call_of_interest_bg_colour_action_, &QAction::triggered, [this] () {
      for (auto item : calls_of_interest_->selectedItems ())
        {
          auto old_call = item->data (Qt::UserRole);
          auto new_colour = QColorDialog::getColor (item->background ().color ()
                                                    , this, tr ("Select background color"));
          if (new_colour.isValid ())
            {
              change_highlighting (old_call.toString (), new_colour, item->foreground ().color ());
              item->setBackground (new_colour);
            }
        }
    });
  calls_of_interest_->insertAction (nullptr, call_of_interest_fg_colour_action_);
  connect (call_of_interest_fg_colour_action_, &QAction::triggered, [this] () {
      for (auto item : calls_of_interest_->selectedItems ())
        {
          auto old_call = item->data (Qt::UserRole);
          auto new_colour = QColorDialog::getColor (item->foreground ().color ()
                                                    , this, tr ("Select foreground color"));
          if (new_colour.isValid ())
            {
              change_highlighting (old_call.toString (), item->background ().color (), new_colour);
              item->setForeground (new_colour);
            }
        }
    });
  connect (calls_of_interest_, &QListWidget::itemChanged, [this] (QListWidgetItem * item) {
      auto old_call = item->data (Qt::UserRole);
      auto new_call = item->text ().toUpper ();
      if (new_call != old_call)
        {
          // tell all clients
          if (old_call.isValid ())
            {
              change_highlighting (old_call.toString ());
            }
          item->setData (Qt::UserRole, new_call);
          item->setText (new_call);
          auto bg = item->listWidget ()->palette ().text ().color ();
          auto fg = item->listWidget ()->palette ().base ().color ();
          item->setBackground (bg);
          item->setForeground (fg);
          change_highlighting (new_call, bg, fg);
        }
    });

  calls_dock->setWidget (calls_of_interest_);
  addDockWidget (Qt::RightDockWidgetArea, calls_dock);
  view_menu_->addAction (calls_dock->toggleViewAction ());
  view_menu_->addSeparator ();

  // connect up server
  connect (server_, &MessageServer::error, [this] (QString const& message) {
      QMessageBox::warning (this, QApplication::applicationName (), tr ("Network Error"), message);
    });
  connect (server_, &MessageServer::client_opened, this, &MessageAggregatorMainWindow::add_client);
  connect (server_, &MessageServer::client_closed, this, &MessageAggregatorMainWindow::remove_client);
  connect (server_, &MessageServer::client_closed, decodes_model_, &DecodesModel::decodes_cleared);
  connect (server_, &MessageServer::client_closed, beacons_model_, &BeaconsModel::decodes_cleared);
  connect (server_, &MessageServer::decode, [this] (bool is_new, QString const& id, QTime time
                                                    , qint32 snr, float delta_time
                                                    , quint32 delta_frequency, QString const& mode
                                                    , QString const& message, bool low_confidence
                                                    , bool off_air) {
             decodes_model_->add_decode (is_new, id, time, snr, delta_time, delta_frequency, mode, message
                                         , low_confidence, off_air, dock_widgets_[id]->fast_mode ());});
  connect (server_, &MessageServer::WSPR_decode, beacons_model_, &BeaconsModel::add_beacon_spot);
  connect (server_, &MessageServer::decodes_cleared, decodes_model_, &DecodesModel::decodes_cleared);
  connect (server_, &MessageServer::decodes_cleared, beacons_model_, &BeaconsModel::decodes_cleared);
  connect (decodes_model_, &DecodesModel::reply, server_, &MessageServer::reply);

  // UI behaviour
  connect (port_spin_box, static_cast<void (QSpinBox::*)(int)> (&QSpinBox::valueChanged)
           , [this] (port_type port) {server_->start (port);});
  connect (multicast_group_line_edit_, &QLineEdit::editingFinished, [this, port_spin_box] () {
      server_->start (port_spin_box->value (), QHostAddress {multicast_group_line_edit_->text ()});
    });

  port_spin_box->setValue (2237); // start up in unicast mode
  show ();
}

void MessageAggregatorMainWindow::log_qso (QString const& /*id*/, QDateTime time_off, QString const& dx_call
                                           , QString const& dx_grid, Frequency dial_frequency, QString const& mode
                                           , QString const& report_sent, QString const& report_received
                                           , QString const& tx_power, QString const& comments
                                           , QString const& name, QDateTime time_on, QString const& operator_call
                                           , QString const& my_call, QString const& my_grid
                                           , QString const& exchange_sent, QString const& exchange_rcvd)
{
  QList<QStandardItem *> row;
  row << new QStandardItem {time_on.toString ("dd-MMM-yyyy hh:mm:ss")}
  << new QStandardItem {time_off.toString ("dd-MMM-yyyy hh:mm:ss")}
  << new QStandardItem {dx_call}
  << new QStandardItem {dx_grid}
  << new QStandardItem {name}
  << new QStandardItem {Radio::frequency_MHz_string (dial_frequency)}
  << new QStandardItem {mode}
  << new QStandardItem {report_sent}
  << new QStandardItem {report_received}
  << new QStandardItem {tx_power}
  << new QStandardItem {operator_call}
  << new QStandardItem {my_call}
  << new QStandardItem {my_grid}
  << new QStandardItem {exchange_sent}
  << new QStandardItem {exchange_rcvd}
  << new QStandardItem {comments};
  log_->appendRow (row);
  log_table_view_->resizeColumnsToContents ();
  log_table_view_->horizontalHeader ()->setStretchLastSection (true);
  log_table_view_->scrollToBottom ();
}

void MessageAggregatorMainWindow::add_client (QString const& id, QString const& version, QString const& revision)
{
  auto dock = new ClientWidget {decodes_model_, beacons_model_, id, version, revision, calls_of_interest_, this};
  dock->setAttribute (Qt::WA_DeleteOnClose);
  auto view_action = dock->toggleViewAction ();
  view_action->setEnabled (true);
  view_menu_->addAction (view_action);
  addDockWidget (Qt::BottomDockWidgetArea, dock);
  connect (server_, &MessageServer::status_update, dock, &ClientWidget::update_status);
  connect (server_, &MessageServer::decode, dock, &ClientWidget::decode_added);
  connect (server_, &MessageServer::WSPR_decode, dock, &ClientWidget::beacon_spot_added);
  connect (server_, &MessageServer::decodes_cleared, dock, &ClientWidget::decodes_cleared);
  connect (dock, &ClientWidget::do_clear_decodes, server_, &MessageServer::clear_decodes);
  connect (dock, &ClientWidget::do_close, server_, &MessageServer::close);
  connect (dock, &ClientWidget::do_reply, decodes_model_, &DecodesModel::do_reply);
  connect (dock, &ClientWidget::do_halt_tx, server_, &MessageServer::halt_tx);
  connect (dock, &ClientWidget::do_free_text, server_, &MessageServer::free_text);
  connect (dock, &ClientWidget::location, server_, &MessageServer::location);
  connect (view_action, &QAction::toggled, dock, &ClientWidget::setVisible);
  connect (dock, &ClientWidget::highlight_callsign, server_, &MessageServer::highlight_callsign);
  connect (dock, &ClientWidget::switch_configuration, server_, &MessageServer::switch_configuration);
  connect (dock, &ClientWidget::configure, server_, &MessageServer::configure);
  dock_widgets_[id] = dock;
  server_->replay (id);         // request decodes and status
}

void MessageAggregatorMainWindow::remove_client (QString const& id)
{
  auto iter = dock_widgets_.find (id);
  if (iter != std::end (dock_widgets_))
    {
      (*iter)->dispose ();
      dock_widgets_.erase (iter);
    }
}

MessageAggregatorMainWindow::~MessageAggregatorMainWindow ()
{
  for (auto client : dock_widgets_)
    {
      delete client;
    }
}

void MessageAggregatorMainWindow::change_highlighting (QString const& call, QColor const& bg, QColor const& fg
                                                       , bool last_only)
{
  for (auto id : dock_widgets_.keys ())
    {
      server_->highlight_callsign (id, call, bg, fg, last_only);
    }
}

#include "moc_MessageAggregatorMainWindow.cpp"
