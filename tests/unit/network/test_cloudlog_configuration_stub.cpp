#include "Configuration.hpp"

namespace
{
  QString stub_cloudlog_api_url;
  QString stub_cloudlog_api_key;
  qint32 stub_cloudlog_api_station_id {0};
  alignas(Configuration) unsigned char configuration_stub_storage[sizeof (Configuration)];
}

namespace test_cloudlog_configuration_stub
{
  void reset ()
  {
    stub_cloudlog_api_url.clear ();
    stub_cloudlog_api_key.clear ();
    stub_cloudlog_api_station_id = 0;
  }

  void setCloudlogValues (QString const& url, QString const& apiKey, qint32 stationId)
  {
    stub_cloudlog_api_url = url;
    stub_cloudlog_api_key = apiKey;
    stub_cloudlog_api_station_id = stationId;
  }

  Configuration const * configuration ()
  {
    return reinterpret_cast<Configuration const *> (configuration_stub_storage);
  }
}

QString Configuration::cloudlog_api_url () const
{
  return stub_cloudlog_api_url;
}

QString Configuration::cloudlog_api_key () const
{
  return stub_cloudlog_api_key;
}

qint32 Configuration::cloudlog_api_station_id () const
{
  return stub_cloudlog_api_station_id;
}
