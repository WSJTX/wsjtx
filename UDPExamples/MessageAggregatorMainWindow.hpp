#ifndef WSJTX_MESSAGE_AGGREGATOR_MAIN_WINDOW_MODEL_HPP__
#define WSJTX_MESSAGE_AGGREGATOR_MAIN_WINDOW_MODEL_HPP__

#include <QMainWindow>
#include <QHash>
#include <QString>

#include "MessageServer.hpp"

class QDateTime;
class QStandardItemModel;
class QMenu;
class DecodesModel;
class BeaconsModel;
class QLineEdit;
class QTableView;
class ClientWidget;
class QListWidget;

using Frequency = MessageServer::Frequency;

class MessageAggregatorMainWindow
  : public QMainWindow
{
  Q_OBJECT;

public:
  MessageAggregatorMainWindow ();
  ~MessageAggregatorMainWindow ();

  Q_SLOT void log_qso (QString const& /*id*/, QDateTime time_off, QString const& dx_call, QString const& dx_grid
                       , Frequency dial_frequency, QString const& mode, QString const& report_sent
                       , QString const& report_received, QString const& tx_power, QString const& comments
                       , QString const& name, QDateTime time_on, QString const& operator_call
                       , QString const& my_call, QString const& my_grid
                       , QString const& exchange_sent, QString const& exchange_rcvd, QString const& prop_mode);

private:
  void add_client (QString const& id, QString const& version, QString const& revision);
  void remove_client (QString const& id);
  void change_highlighting (QString const& call, QColor const& bg = QColor {}, QColor const& fg = QColor {},
                            bool last_only = false);

  // maps client id to widgets
  using ClientsDictionary = QHash<QString, ClientWidget *>;
  ClientsDictionary dock_widgets_;

  QStandardItemModel * log_;
  QMenu * view_menu_;
  DecodesModel * decodes_model_;
  BeaconsModel * beacons_model_;
  MessageServer * server_;
  QLineEdit * multicast_group_line_edit_;
  QTableView * log_table_view_;
  QListWidget * calls_of_interest_;
  QAction * add_call_of_interest_action_;
  QAction * delete_call_of_interest_action_;
  QAction * last_call_of_interest_action_;
  QAction * call_of_interest_bg_colour_action_;
  QAction * call_of_interest_fg_colour_action_;
};

#endif
