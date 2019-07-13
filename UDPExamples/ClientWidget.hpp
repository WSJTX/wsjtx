#ifndef WSJTX_UDP_CLIENT_WIDGET_MODEL_HPP__
#define WSJTX_UDP_CLIENT_WIDGET_MODEL_HPP__

#include <QDockWidget>
#include <QObject>
#include <QSortFilterProxyModel>
#include <QString>
#include <QRegularExpression>

#include "MessageServer.hpp"

class QAbstractItemModel;
class QModelIndex;
class QColor;
class QAction;
class QListWidget;
class QFormLayout;
class QVBoxLayout;
class QHBoxLayout;
class QStackedLayout;
class QTableView;
class QLineEdit;
class QAbstractButton;
class QLabel;
class QCheckBox;
class QSpinBox;
class QFrame;
class QStatusBar;
class QDialogButtonBox;

using Frequency = MessageServer::Frequency;

class ClientWidget
  : public QDockWidget
{
  Q_OBJECT;

public:
  explicit ClientWidget (QAbstractItemModel * decodes_model, QAbstractItemModel * beacons_model
                         , QString const& id, QString const& version, QString const& revision
                         , QListWidget const * calls_of_interest, QWidget * parent = nullptr);
  void dispose ();
  ~ClientWidget ();

  bool fast_mode () const;

  Q_SLOT void update_status (QString const& id, Frequency f, QString const& mode, QString const& dx_call
                             , QString const& report, QString const& tx_mode, bool tx_enabled
                             , bool transmitting, bool decoding, quint32 rx_df, quint32 tx_df
                             , QString const& de_call, QString const& de_grid, QString const& dx_grid
                             , bool watchdog_timeout, QString const& sub_mode, bool fast_mode
                             , quint8 special_op_mode, quint32 frequency_tolerance, quint32 tr_period
                             , QString const& configuration_name);
  Q_SLOT void decode_added (bool is_new, QString const& client_id, QTime, qint32 snr
                            , float delta_time, quint32 delta_frequency, QString const& mode
                            , QString const& message, bool low_confidence, bool off_air);
  Q_SLOT void beacon_spot_added (bool is_new, QString const& client_id, QTime, qint32 snr
                                 , float delta_time, Frequency delta_frequency, qint32 drift
                                 , QString const& callsign, QString const& grid, qint32 power
                                 , bool off_air);
  Q_SLOT void decodes_cleared (QString const& client_id);

  Q_SIGNAL void do_clear_decodes (QString const& id, quint8 window = 0);
  Q_SIGNAL void do_close (QString const& id);
  Q_SIGNAL void do_reply (QModelIndex const&, quint8 modifier);
  Q_SIGNAL void do_halt_tx (QString const& id, bool auto_only);
  Q_SIGNAL void do_free_text (QString const& id, QString const& text, bool);
  Q_SIGNAL void location (QString const& id, QString const& text);
  Q_SIGNAL void highlight_callsign (QString const& id, QString const& call
                                    , QColor const& bg = QColor {}, QColor const& fg = QColor {}
                                    , bool last_only = false);
  Q_SIGNAL void switch_configuration (QString const& id, QString const& configuration_name);
  Q_SIGNAL void configure (QString const& id, QString const& mode, quint32 frequency_tolerance
                           , QString const& submode, bool fast_mode, quint32 tr_period, quint32 rx_df
                           , QString const& dx_call, QString const& dx_grid, bool generate_messages);

private:
  class IdFilterModel final
    : public QSortFilterProxyModel
  {
  public:
    IdFilterModel (QString const& client_id, QObject * = nullptr);

    void de_call (QString const&);
    void rx_df (quint32);

    QVariant data (QModelIndex const& proxy_index, int role = Qt::DisplayRole) const override;
  private:
    bool filterAcceptsRow (int source_row, QModelIndex const& source_parent) const override;

    QString client_id_;
    QString call_;
    QRegularExpression base_call_re_;
    quint32 rx_df_;
  };

  void closeEvent (QCloseEvent *) override;

  QString id_;
  bool done_;
  QListWidget const * calls_of_interest_;
  IdFilterModel decodes_proxy_model_;
  IdFilterModel beacons_proxy_model_;

  QAction * erase_action_;
  QAction * erase_rx_frequency_action_;
  QAction * erase_both_action_;
  QTableView * decodes_table_view_;
  QTableView * beacons_table_view_;
  QLineEdit * message_line_edit_;
  QLineEdit * grid_line_edit_;
  QAbstractButton * generate_messages_push_button_;
  QAbstractButton * auto_off_button_;
  QAbstractButton * halt_tx_button_;
  QLabel * de_label_;
  QLabel * frequency_label_;
  QLabel * tx_df_label_;
  QLabel * report_label_;
  QLineEdit * configuration_line_edit_;
  QLineEdit * mode_line_edit_;
  QSpinBox * frequency_tolerance_spin_box_;
  QLabel * tx_mode_label_;
  QLineEdit * submode_line_edit_;
  QCheckBox * fast_mode_check_box_;
  QSpinBox * tr_period_spin_box_;
  QSpinBox * rx_df_spin_box_;
  QLineEdit * dx_call_line_edit_;
  QLineEdit * dx_grid_line_edit_;
  QWidget * decodes_page_;
  QWidget * beacons_page_;
  QFrame * content_widget_;
  QStatusBar * status_bar_;
  QDialogButtonBox * control_button_box_;

  QFormLayout * form_layout_;
  QHBoxLayout * horizontal_layout_;
  QFormLayout * subform1_layout_;
  QFormLayout * subform2_layout_;
  QFormLayout * subform3_layout_;
  QVBoxLayout * decodes_layout_;
  QVBoxLayout * beacons_layout_;
  QVBoxLayout * content_layout_;
  QStackedLayout * decodes_stack_;

  bool columns_resized_;
};

#endif
