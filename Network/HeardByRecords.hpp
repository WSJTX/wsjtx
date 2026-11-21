#ifndef HEARD_BY_RECORDS_HPP__
#define HEARD_BY_RECORDS_HPP__

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;

//
// HeardByRecords - the reception reports PSK Reporter holds for one
// callsign as sender (its "retrieve" query), newest first.
//
// PSK Reporter asks for no more than one query per 5 minutes, so at
// most one is made per min_interval_ms and never while one is
// running.  Nothing is sent but the callsign asked about and a
// generic User-Agent.
//
class HeardByRecords final
  : public QObject
{
  Q_OBJECT

public:
  struct Report
  {
    QString receiver;
    QString locator;            // receiver's locator
    QString mode;               // as PSK Reporter names it
    quint64 frequency {0};      // Hz
    int snr {0};
    qint64 when {0};            // epoch seconds
    qint64 id {0};              // from receiver, time and frequency
  };

  struct Result
  {
    bool ok {false};
    QString error;
    QList<Report> reports;      // newest first
  };

  static int constexpr default_interval_ms = 5 * 60 * 1000;

  explicit HeardByRecords (QNetworkAccessManager *, QObject * parent = nullptr,
                           QString const& base_url = "https://retrieve.pskreporter.info/query",
                           int min_interval_ms = default_interval_ms);

  // Ask for the reports of call in the last lookback_secs.  False
  // (nothing sent) if the last query was too recent or one is running.
  bool fetch (QString const& call, int lookback_secs);
  bool busy () const {return busy_;}

  // Milliseconds until the next query is allowed (0 = now).
  int ms_until_next () const;

  // Epoch milliseconds of the last query (0 = never).  The caller
  // saves it and restores it at the next start so that a restart does
  // not ask again too soon.
  qint64 last_query_ms () const {return last_ms_;}
  void set_last_query_ms (qint64 ms) {last_ms_ = ms;}

  // A delay for the next poll: the minimum interval plus up to
  // jitter_ms, so that many stations do not poll in step.
  static int poll_delay_ms (int min_interval_ms = default_interval_ms, int jitter_ms = 45 * 1000);

  // The reports in a retrieve reply (XML).
  static Result parse (QByteArray const& xml);
  static qint64 report_id (QString const& receiver, qint64 when, quint64 frequency);

  Q_SIGNAL void finished (HeardByRecords::Result const&);

private:
  QNetworkAccessManager * network_;
  QString base_url_;
  int min_interval_ms_;
  qint64 last_ms_ {0};
  bool busy_ {false};
};

Q_DECLARE_METATYPE (HeardByRecords::Result)

#endif
