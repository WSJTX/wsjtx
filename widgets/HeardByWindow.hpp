#ifndef HEARD_BY_WINDOW_HPP__
#define HEARD_BY_WINDOW_HPP__

#include <functional>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <QWidget>

#include "Network/HeardByMqtt.hpp"
#include "models/HeardByModel.hpp"

class QFont;
class QLabel;
class QSettings;
class QTableView;

//
// HeardByWindow - which stations report our signal on PSK Reporter.
//
// The reports come over PSK Reporter's live MQTT feed (HeardByMqtt),
// within a few minutes of being heard.  PSK Reporter's query endpoint
// is not used: with this many WSJT-X stations it would be a real load.
// So the list starts empty and fills as reports arrive; it is kept
// while WSJT-X runs, also when the window is closed and opened again.
//
// The feed can stop delivering while the connection stays up.  After
// silent_after_ms without a report of our own a look at the broker's
// own status topic (HeardByMqtt::check_feed) tells "nobody reports
// you" from "the feed is silent", shown in a banner.  Only our own
// call's spots are ever subscribed to.
// Nothing is asked of PSK Reporter unless this window is open.  Call,
// grid, mode and band come from the main window through the context
// callback, once a second.
//
class HeardByWindow final
  : public QWidget
{
  Q_OBJECT

public:
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

  HeardByWindow (QSettings *, QFont const&, ContextProvider,
                 HeardByModel::BandOf, HeardByModel::DistanceTo, QWidget * parent = nullptr);

  void change_font (QFont const&);

  // for tests
  HeardByModel * model () {return &model_;}
  HeardByMqtt * mqtt () {return &mqtt_;}
  void set_mqtt (HeardByMqtt::Options const& feed, Timing const&);
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
  void update_status ();
  void fit_width ();            // no wider than the columns need
  void stop_feed ();
  void on_spot (QJsonObject const&);
  void feed_checked (bool delivering);

  enum class Feed {Unknown, Alive, Silent};

  QSettings * settings_;
  ContextProvider context_;
  HeardByModel::BandOf band_of_;
  HeardByMqtt mqtt_;
  Timing timing_;
  HeardByModel model_;
  QTableView * table_;
  QLabel * banner_;             // is the feed delivering?
  QLabel * status_;
  QLabel * credit_;
  QLabel * in_use_;             // mode and band read from the main window
  QTimer timer_;
  Context last_;
  QElapsedTimer clock_;
  qint64 own_ref_ms_ {0};       // last own spot, or when the subscription began
  qint64 last_check_ms_ {-1};
  Feed feed_ {Feed::Unknown};
  QString feed_checked_;        // "HH:mm" UTC of the last check
};

#endif
