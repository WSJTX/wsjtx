#ifndef HEARD_BY_WINDOW_HPP__
#define HEARD_BY_WINDOW_HPP__

#include <functional>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <QWidget>

#include "Network/HeardByMqtt.hpp"
#include "Network/HeardByRecords.hpp"
#include "models/HeardByModel.hpp"

class QComboBox;
class QFont;
class QLabel;
class QNetworkAccessManager;
class QSettings;
class QTableView;

//
// HeardByWindow - which stations report our signal on PSK Reporter.
//
// Two sources for the same list, the user's choice:
//   - the live MQTT feed (HeardByMqtt, the default): reports within a
//     few minutes, but the feed can stop delivering while the
//     connection stays up.
//     After silent_after_ms without a report of our own a look at
//     the broker's own status topic (HeardByMqtt::check_feed) tells
//     "nobody reports you" from "the feed is silent", shown in a
//     banner.  Only our own call's spots are ever subscribed to.  One
//     records query fills the list with what came before;
//   - PSK Reporter's records: polled at most once per 5 minutes (see
//     HeardByRecords).
// Nothing is asked of PSK Reporter unless this window is open.  Call,
// grid, mode and band come from the main window through the context
// callback, once a second.
//
class HeardByWindow final
  : public QWidget
{
  Q_OBJECT

public:
  enum class Source {Records, Mqtt};

  struct Timing
  {
    int silent_after_ms {5 * 60 * 1000};    // own spots quiet this long: check the feed
    int check_window_ms {5 * 1000};         // how long the status topic is watched
    int check_every_ms {10 * 60 * 1000};    // and how often at most
  };

  struct Context
  {
    QString call;
    QString grid;
    QString mode;               // e.g. "Q65", "FT8"
    QString band;               // WSJT-X band name of the Rx frequency
  };
  using ContextProvider = std::function<Context ()>;

  HeardByWindow (QSettings *, QFont const&, QNetworkAccessManager *, ContextProvider,
                 HeardByModel::BandOf, HeardByModel::DistanceTo, QWidget * parent = nullptr,
                 QString const& records_url = "https://retrieve.pskreporter.info/query");

  void change_font (QFont const&);

  // for tests
  HeardByModel * model () {return &model_;}
  HeardByRecords * records () {return &records_;}
  HeardByMqtt * mqtt () {return &mqtt_;}
  void set_mqtt (HeardByMqtt::Options const& feed, Timing const&);
  Source source () const;
  void set_source (Source);
  QString status_text () const;
  QString in_use_text () const;
  QString banner_text () const;
  void tick ();                 // what the once-a-second timer does

protected:
  void showEvent (QShowEvent *) override;
  void hideEvent (QHideEvent *) override;
  void closeEvent (QCloseEvent *) override;

private:
  void read_settings ();
  void write_settings ();
  void on_records (HeardByRecords::Result const&);
  void update_status ();
  void fit_width ();            // no wider than the columns need
  void restart ();              // the call or the source changed
  void stop_feed ();
  void on_spot (QJsonObject const&);
  void feed_checked (bool delivering);

  enum class Feed {Unknown, Alive, Silent};

  QSettings * settings_;
  ContextProvider context_;
  HeardByModel::BandOf band_of_;
  HeardByRecords records_;
  HeardByMqtt mqtt_;
  Timing timing_;
  HeardByModel model_;
  QComboBox * source_box_;
  QTableView * table_;
  QLabel * banner_;             // MQTT: is the feed delivering?
  QLabel * status_;
  QLabel * credit_;
  QLabel * in_use_;             // mode and band read from the main window
  QTimer timer_;
  Context last_;
  QString error_;
  qint64 next_poll_ms_ {0};     // epoch ms
  bool first_result_ {false};
  bool asked_ {false};
  bool skip_history_ {false};   // switched to MQTT in view: live rows only
  QElapsedTimer clock_;
  qint64 own_ref_ms_ {0};       // MQTT: last own spot, or when the subscription began
  qint64 last_check_ms_ {-1};
  Feed feed_ {Feed::Unknown};
  QString feed_checked_;        // "HH:mm" UTC of the last check
};

#endif
