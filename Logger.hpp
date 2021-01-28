#ifndef LOGGER_HPP__
#define LOGGER_HPP__

#include <boost/log/trivial.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/attributes/mutable_constant.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <iosfwd>
#include <string>

BOOST_LOG_GLOBAL_LOGGER (sys,
                         boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level>);
BOOST_LOG_GLOBAL_LOGGER (data,
                         boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level>);

namespace Logger
{
  // trivial logging to console
  void init ();

  // define logger(s) and sinks from a configuration stream
  void init_from_config (std::wistream& config_stream);

  // disable logging - useful for unit testing etc.
  void disable ();

  // add a new file sink for LOG_DATA_* for Severity >= INFO
  // this file sink will be used alongside any configured above
  void add_data_file_log (std::wstring const& log_file_name);
}

#define LOG_LOG_LOCATION(LOGGER, LEVEL, ARG)                  \
  BOOST_LOG_SEV (LOGGER, boost::log::trivial::LEVEL)          \
  << boost::log::add_value ("Line", __LINE__)                 \
  << boost::log::add_value ("File", __FILE__)                 \
  << boost::log::add_value ("Function", __FUNCTION__) << ARG
 
/// System Log macros.
/// TRACE < DEBUG < INFO < WARN < ERROR < FATAL
#define LOG_TRACE(ARG) LOG_LOG_LOCATION (sys::get(), trace, ARG)
#define LOG_DEBUG(ARG) LOG_LOG_LOCATION (sys::get(), debug, ARG)
#define LOG_INFO(ARG)  LOG_LOG_LOCATION (sys::get(), info, ARG)
#define LOG_WARN(ARG)  LOG_LOG_LOCATION (sys::get(), warning, ARG)
#define LOG_ERROR(ARG) LOG_LOG_LOCATION (sys::get(), error, ARG)
#define LOG_FATAL(ARG) LOG_LOG_LOCATION (sys::get(), fatal, ARG)
 
/// Data Log macros. Does not include LINE, FILE, FUNCTION.
/// TRACE < DEBUG < INFO < WARN < ERROR < FATAL
#define LOG_DATA_TRACE(ARG) BOOST_LOG_SEV (data::get(), boost::log::trivial::trace) << ARG
#define LOG_DATA_DEBUG(ARG) BOOST_LOG_SEV (data::get(), boost::log::trivial::debug) << ARG
#define LOG_DATA_INFO(ARG)  BOOST_LOG_SEV (data::get(), boost::log::trivial::info) << ARG
#define LOG_DATA_WARN(ARG)  BOOST_LOG_SEV (data::get(), boost::log::trivial::warning) << ARG
#define LOG_DATA_ERROR(ARG) BOOST_LOG_SEV (data::get(), boost::log::trivial::error) << ARG
#define LOG_DATA_FATAL(ARG) BOOST_LOG_SEV (data::get(), boost::log::trivial::fatal) << ARG

#endif
