#include "Logger.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/core.hpp>
#include <boost/log/common.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/expressions/keyword.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/attributes/clock.hpp>
#include <boost/log/attributes/counter.hpp>
#include <boost/log/attributes/current_process_id.hpp>
#include <boost/log/attributes/current_thread_id.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/filter_parser.hpp>
#include <boost/log/utility/setup/from_stream.hpp>
#include <boost/log/utility/setup/settings.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sinks/debug_output_backend.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
 
#include <fstream>
#include <string>

namespace logging = boost::log;
namespace srcs = logging::sources;
namespace sinks = logging::sinks;
namespace keywords = logging::keywords;
namespace expr = logging::expressions;
namespace attrs = logging::attributes;
namespace ptime = boost::posix_time;

BOOST_LOG_GLOBAL_LOGGER_CTOR_ARGS (sys,
                                   srcs::severity_channel_logger_mt<logging::trivial::severity_level>,
                                   (keywords::channel = "SYSLOG"));
BOOST_LOG_GLOBAL_LOGGER_CTOR_ARGS (data,
                                   srcs::severity_channel_logger_mt<logging::trivial::severity_level>,
                                   (keywords::channel = "DATALOG"));

namespace Logger
{
  namespace
  {
    // Custom formatter factory to add TimeStamp format support in config ini file.
    // Allows %TimeStamp(format=\"%Y.%m.%d %H:%M:%S.%f\")% to be used in ini config file for property Format.
    class TimeStampFormatterFactory
      : public logging::basic_formatter_factory<char, ptime::ptime>
    {
    public:
      formatter_type create_formatter (logging::attribute_name const& name, args_map const& args)
      {
        args_map::const_iterator it = args.find ("format");
        if (it != args.end ())
          {
            return expr::stream 
              << expr::format_date_time<ptime::ptime>
              (
               expr::attr<ptime::ptime> (name), it->second
               );
          }
        else
          {
            return expr::stream 
              << expr::attr<ptime::ptime> (name);
          }
      }
    };

    // Custom formatter factory to add Uptime format support in config ini file.
    // Allows %Uptime(format=\"%O:%M:%S.%f\")% to be used in ini config file for property Format.
    // attrs::timer value type is ptime::time_duration
    class UptimeFormatterFactory
      : public logging::basic_formatter_factory<char, ptime::time_duration>
    {
    public:
      formatter_type create_formatter (logging::attribute_name const& name, args_map const& args)
      {
        args_map::const_iterator it = args.find ("format");
        if (it != args.end ())
          {
            return expr::stream
              << expr::format_date_time<ptime::time_duration>
              (
               expr::attr<ptime::time_duration> (name), it->second
               );
          }
        else
          {
            return expr::stream
              << expr::attr<ptime::time_duration> (name);
          }
      }
    };

    class CommonInitialization
    {
    public:
      CommonInitialization ()
      {
        // Add attributes: LineID, TimeStamp, ProcessID, ThreadID, and Uptime
        auto core = logging::core::get ();
        core->add_global_attribute ("LineID", attrs::counter<unsigned int> (1));
        core->add_global_attribute ("TimeStamp", attrs::utc_clock ());
        core->add_global_attribute ("ProcessID", attrs::current_process_id ());
        core->add_global_attribute ("ThreadID", attrs::current_thread_id ());
        core->add_global_attribute ("Uptime", attrs::timer ());

        // Allows %Severity% to be used in ini config file for property Filter.
        logging::register_simple_filter_factory<logging::trivial::severity_level, char> ("Severity");
        // Allows %Severity% to be used in ini config file for property Format.
        logging::register_simple_formatter_factory<logging::trivial::severity_level, char> ("Severity");
        // Allows %TimeStamp(format=\"%Y.%m.%d %H:%M:%S.%f\")% to be used in ini config file for property Format.
        logging::register_formatter_factory ("TimeStamp", boost::make_shared<TimeStampFormatterFactory> ());
        // Allows %Uptime(format=\"%O:%M:%S.%f\")% to be used in ini config file for property Format.
        logging::register_formatter_factory ("Uptime", boost::make_shared<UptimeFormatterFactory> ());
      }
      ~CommonInitialization ()
      {
      }
    };
  }

  void init ()
  {
    CommonInitialization ci;
  }

  void init_from_config (std::istream& stream)
  {
    CommonInitialization ci;
    try
      {
        // Still can throw even with the exception suppressor above.
        logging::init_from_stream (stream);
      }
    catch (std::exception& e)
      {
        std::string err = "Caught exception initializing boost logging: ";
        err += e.what ();
        // Since we cannot be sure of boost log state, output to cerr and cout.
        std::cerr << "ERROR: " << err << std::endl;
        std::cout << "ERROR: " << err << std::endl;
        LOG_ERROR (err);
      }
  }

  void disable ()
  {
    logging::core::get ()->set_logging_enabled (false);
  }

  void add_datafile_log (std::string const& log_file_name)
  {
    // Create a text file sink
    boost::shared_ptr<sinks::text_ostream_backend> backend
      (
       new sinks::text_ostream_backend()
       );
    backend->add_stream (boost::shared_ptr<std::ostream> (new std::ofstream (log_file_name)));
     
    // Flush after each log record
    backend->auto_flush (true);
     
    // Create a sink for the backend
    typedef sinks::synchronous_sink<sinks::text_ostream_backend> sink_t;
    boost::shared_ptr<sink_t> sink (new sink_t (backend));
     
    // The log output formatter
    sink->set_formatter (expr::format ("[%1%][%2%] %3%")
                         % expr::attr<ptime::ptime> ("TimeStamp")
                         % logging::trivial::severity
                         % expr::message
                         );
     
    // Filter by severity and by DATALOG channel
    sink->set_filter (logging::trivial::severity >= logging::trivial::info &&
                      expr::attr<std::string> ("Channel") == "DATALOG");
     
    // Add it to the core
    logging::core::get ()->add_sink (sink);
  }
}
