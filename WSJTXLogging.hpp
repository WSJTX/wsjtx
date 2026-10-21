#ifndef WSJTX_LOGGING_HPP__
#define WSJTX_LOGGING_HPP__

#include <string>
#include <boost/log/expressions/filter.hpp>
#include <boost/log/expressions/attr.hpp>
#include <boost/log/expressions/predicates/channel_severity_filter.hpp>
#include <boost/log/trivial.hpp>
#include <boost/phoenix/operator/comparison.hpp>
#include <boost/phoenix/operator/logical.hpp>

//
// Class WSJTXLogging - wraps application specific logging
//
class WSJTXLogging final
{
public:
  explicit WSJTXLogging ();
  ~WSJTXLogging ();

  static boost::log::filter default_filter ()
  {
    namespace expr = boost::log::expressions;
    namespace trivial = boost::log::trivial;
    auto severity = expr::attr<trivial::severity_level> ("Severity");
    auto minimum = expr::channel_severity_filter (expr::attr<std::string> ("Channel"), severity);
    minimum["SYSLOG"] = trivial::error;
    minimum["RIGCTRL"] = trivial::warning;
    minimum["DATALOG"] = trivial::error;
    minimum["DECODER"] = trivial::warning;
    return minimum || severity >= trivial::fatal;
  }
};

#endif
