#ifndef FREQUENCY_CONTROL_HPP
#define FREQUENCY_CONTROL_HPP

#include <QtGlobal>
#include <QMetaType>
#include "Radio.hpp"

namespace RigFrequency
{
  enum class Path { Rx, Tx };
  enum class Outcome { Pending, Deferred, Established, Rejected, Failed };
  enum class Basis { Retained, BackendOperation, Readback, Emulated };

  inline Radio::Frequency resolution_hz (int code)
  {
    if (code >= 3 || code <= -3) return 100;
    if (code == 2 || code == -2) return 20;
    if (code == 1 || code == -1) return 10;
    return 1;
  }

  inline bool within_resolution (Radio::Frequency observed, Radio::Frequency target,
                                 Radio::Frequency resolution)
  {
    auto const delta = observed > target ? observed - target : target - observed;
    return delta < resolution;
  }

  struct Request
  {
    quint64 generation = 0;
    quint64 id = 0;
    Path path = Path::Rx;
    Radio::Frequency target = 0;
    explicit operator bool () const { return id != 0; }
  };

  inline bool same_operation (Request const& a, Request const& b)
  { return a.generation == b.generation && a.id == b.id && a.path == b.path; }

  struct Result
  {
    Request request;
    Outcome outcome = Outcome::Pending;
    Basis basis = Basis::Retained;
    Radio::Frequency applied = 0;
  };

  struct Evidence
  {
    quint64 generation = 0;
    quint64 serial = 0;
    Path path = Path::Rx;
    Radio::Frequency value = 0;
    Basis basis = Basis::Readback;
    Request operation;
  };
}

Q_DECLARE_METATYPE (RigFrequency::Request)
Q_DECLARE_METATYPE (RigFrequency::Result)
Q_DECLARE_METATYPE (RigFrequency::Evidence)

#endif
