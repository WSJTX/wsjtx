#ifndef CLOUDLOG_CONFIGURATION_HPP_
#define CLOUDLOG_CONFIGURATION_HPP_

#include <QString>

class CloudlogConfiguration
{
public:
  virtual ~CloudlogConfiguration () = default;

  virtual QString cloudlog_api_url () const = 0;
  virtual QString cloudlog_api_key () const = 0;
  virtual qint32 cloudlog_api_station_id () const = 0;
};

#endif
