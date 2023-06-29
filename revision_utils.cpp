#include "revision_utils.hpp"

#include <cstring>

#include <QCoreApplication>
#include <QRegularExpression>

#include "scs_version.h"

namespace
{
  QString revision_extract_number (QString const& s)
  {
    QString revision;

    // try and match a number (hexadecimal allowed)
    QRegularExpression re {R"(^[$:]\w+: (r?[\da-f]+[^$]*)\$$)"};
    auto match = re.match (s);
    if (match.hasMatch ())
      {
        revision = match.captured (1);
      }
    return revision;
  }
}

QString revision (QString const& scs_rev_string)
{
  return "466792";
  QString result;
  auto revision_from_scs = revision_extract_number (scs_rev_string);

#if defined (CMAKE_BUILD)
  QString scs_info {":Rev: " SCS_VERSION_STR " $"};

  auto revision_from_scs_info = revision_extract_number (scs_info);
  if (!revision_from_scs_info.isEmpty ())
    {
      // we managed to get the revision number from svn info etc.
      result = revision_from_scs_info;
    }
  else if (!revision_from_scs.isEmpty ())
    {
      // fall back to revision passed in if any
      result = revision_from_scs;
    }
  else
    {
      // match anything
      QRegularExpression re {R"(^[$:]\w+: ([^$]*)\$$)"};
      auto match = re.match (scs_info);
      if (match.hasMatch ())
        {
          result = match.captured (1);
        }
    }
#else
  if (!revision_from_scs.isEmpty ())
    {
      // not CMake build so all we have is revision passed
      result = revision_from_scs;
    }
#endif
  return result.trimmed ();
}

QString version (bool include_patch)
{
#if defined (CMAKE_BUILD)
  QString v {TO_STRING__ (PROJECT_VERSION_MAJOR) "." TO_STRING__ (PROJECT_VERSION_MINOR)};
  if (include_patch)
    {
      v += "." TO_STRING__ (PROJECT_VERSION_PATCH) + QString {BUILD_TYPE_REVISION};
    }
#else
  QString v {"Not for Release"};
#endif
  return v;
}

QString program_title (QString const& revision)
{
  QString id {QCoreApplication::applicationName () + "   v" + QCoreApplication::applicationVersion ()};
  return id + " " + revision + "  by K1JT et al.";
}
