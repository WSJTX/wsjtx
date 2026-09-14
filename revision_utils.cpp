#include "revision_utils.hpp"

#include <cstring>

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSysInfo>

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
  //  return "251203";
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

QString display_revision ()
{
  auto build_revision = revision ();

#if defined (CMAKE_BUILD) && defined (WSJT_SOURCE_REVISION)
  QString source_revision {WSJT_SOURCE_REVISION};
  if (!source_revision.isEmpty ())
    {
      source_revision = source_revision.left (6);
      if (source_revision != build_revision.left (6))
        {
          return QString {"(source %1, build %2)"}.arg (
            source_revision,
            build_revision.isEmpty () ? QString {"unavailable"} : build_revision);
        }
    }
#endif

  if (build_revision.isEmpty ())
    {
      return QString {"(revision unavailable)"};
    }

  return QString {"(revision %1)"}.arg (build_revision);
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
  // applicationName() itself must stay plain ASCII -- it also names the jt9 shared-memory key and settings/lock/temp paths.
  QString id {QCoreApplication::applicationName () + "™   v" + QCoreApplication::applicationVersion ()};
  return id + " " + revision ;
}

QString http_user_agent ()
{
  // See User-Agent format definition https://www.rfc-editor.org/rfc/rfc9110#name-user-agent
  QString const platform {
    "(" + QSysInfo::prettyProductName () + "; "
    + QSysInfo::productType () + " " + QSysInfo::productVersion () + "; "
    + QSysInfo::currentCpuArchitecture () + "; "
    + QString {"rv:%1"}.arg (QSysInfo::kernelVersion ()) + ")"};

  return QString {"WSJT-X/" + version () + "_" + revision ()}.simplified () + " " + platform;
}

QString copyright_notice_text ()
{
  return QCoreApplication::translate (
    "main",
    "If you make fair use of any part of WSJT-X, MAP65, QMAP, or our "
    "associated utility programs under terms of the GNU General Public "
    "License, you must display the following copyright notice prominently "
    "in your derivative work:\n\n"
    "\"The algorithms, source code, look-and-feel of WSJT-X, MAP65, QMAP, "
    "and related programs, and protocol specifications for the modes "
    "FSK441, FST4, FST4W, FT4, FT8, ISCAT, JT4, JT6M, JT9, JT65, JTMS, "
    "JTTY, MSK144, QRA64, Q65, and WSPR are Copyright (C) 2001-2026 by one "
    "or more of the following authors: Joseph Taylor, K1JT; Bill "
    "Somerville, G4WJS; Steven Franke, K9AN; Nico Palermo, IV3NWV; Greg "
    "Beam, KI7MT; Michael Black, W9MDB; Edson Pereira, PY2SDR; Philip Karn, "
    "KA9Q; Uwe Risse, DG2YCB; Brian Moran, N9ADG; Roger Rehr, W3SZ; John "
    "Nelson, G4KLA; Charlie Suckling, DL3WDG; Terrell Deppe, KJ5HST; David "
    "Christle, KD0BTO; and other members of the WSJT™ Development Team.\"");
}
