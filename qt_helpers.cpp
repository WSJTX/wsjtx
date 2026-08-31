#include "qt_helpers.hpp"

#include <QString>
#include <QFont>
#include <QWidget>
#include <QStyle>
#include <QVariant>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <limits>

#if CMAKE_BUILD
#include "wsjtx_config.h"
#endif

QString font_as_stylesheet (QFont const& font)
{
  QString font_weight;
  switch (font.weight ())
    {
    case QFont::Light: font_weight = "light"; break;
    case QFont::Normal: font_weight = "normal"; break;
    case QFont::DemiBold: font_weight = "demibold"; break;
    case QFont::Bold: font_weight = "bold"; break;
    case QFont::Black: font_weight = "black"; break;
    }
  return QString {
      " font-family: %1;\n"
      " font-size: %2pt;\n"
      " font-style: %3;\n"
      " font-weight: %4;\n"}
  .arg (font.family ())
     .arg (font.pointSize ())
     .arg (font.styleName ())
     .arg (font_weight);
}

QString application_style_sheet (QString const& base_style_sheet,
                                 QString const& dark_style_sheet,
                                 bool dark_style,
                                 QFont const& font)
{
  auto style_sheet = dark_style ? dark_style_sheet : base_style_sheet;
  if (!style_sheet.isEmpty ())
    {
      style_sheet += '\n';
    }
  return style_sheet + "* {" + font_as_stylesheet (font) + '}';
}

void update_dynamic_property (QWidget * widget, char const * property, QVariant const& value)
{
  widget->setProperty (property, value);
  widget->style ()->unpolish (widget);
  widget->style ()->polish (widget);
  widget->update ();
}

namespace
{
enum class DateTimeAlignment
{
  earlier,
  nearest
};

QDateTime align_date_time (QDateTime dt, int milliseconds, DateTimeAlignment alignment)
{
  if (!dt.isValid () || milliseconds <= 0)
    {
      return {};
    }

  auto const interval = static_cast<qint64> (milliseconds);
  auto const timestamp = dt.toMSecsSinceEpoch ();
  auto remainder = timestamp % interval;
  if (remainder < 0)
    {
      remainder += interval;
    }

  auto offset = -remainder;
  if (alignment == DateTimeAlignment::nearest && remainder >= interval - remainder)
    {
      offset = interval - remainder;
    }

  if ((offset > 0 && timestamp > std::numeric_limits<qint64>::max () - offset)
      || (offset < 0 && timestamp < std::numeric_limits<qint64>::min () - offset))
    {
      return {};
    }

  dt.setMSecsSinceEpoch (timestamp + offset);
  return dt;
}
}

QDateTime qt_round_date_time_to (QDateTime dt, int milliseconds)
{
  return align_date_time (dt, milliseconds, DateTimeAlignment::nearest);
}

QDateTime qt_truncate_date_time_to (QDateTime dt, int milliseconds)
{
  return align_date_time (dt, milliseconds, DateTimeAlignment::earlier);
}

namespace
{
QDir legacy_app_sounds_directory ()
{
#if defined (__APPLE__)
  return QDir {QCoreApplication::applicationDirPath () + "/../Resources/sounds"};
#else
  return QDir {QCoreApplication::applicationDirPath () + "/sounds"};
#endif
}

QString normalized_app_sounds_child (QString const& subdirectory)
{
  auto child = subdirectory.trimmed ();
  child.replace (QChar {'\\'}, QChar {'/'});
  while (child.startsWith (QChar {'/'}))
    {
      child.remove (0, 1);
    }
  return QDir::cleanPath (child);
}

bool normalized_app_sounds_child_is_safe (QString const& child)
{
  return child.isEmpty () || child == "."
    || (!QDir::isAbsolutePath (child) && child != ".." && !child.startsWith ("../"));
}
}

QDir resolve_installed_data_directory (QString const& application_directory,
                                       QString const& configured_data_destination,
                                       QString const& relative_application_root)
{
  if (QDir::isAbsolutePath (configured_data_destination))
    {
      return QDir {QDir::cleanPath (configured_data_destination)};
    }

  QDir application {application_directory};
  auto const destination = QDir::cleanPath (
    application.absoluteFilePath (relative_application_root + QChar {'/'} + configured_data_destination));
  return QDir {destination};
}

QDir installed_data_directory ()
{
#if CMAKE_BUILD
#if defined (Q_OS_MAC)
  auto const relative_application_root = QStringLiteral ("../../..");
#else
  auto const relative_application_root = QStringLiteral ("..");
#endif
  return resolve_installed_data_directory (QCoreApplication::applicationDirPath (),
                                           QString::fromUtf8 (WSJT_DATA_DESTINATION),
                                           relative_application_root);
#else
  return QDir {QCoreApplication::applicationDirPath ()};
#endif
}

QDir preferred_sounds_directory (QDir const& canonical_directory, QDir const& legacy_directory)
{
  if (canonical_directory.exists ())
    {
      return canonical_directory;
    }
  if (legacy_directory.exists ())
    {
      return legacy_directory;
    }
  return canonical_directory;
}

QDir app_sounds_directory ()
{
  auto const legacy_directory = legacy_app_sounds_directory ();
#if defined (Q_OS_LINUX)
  return preferred_sounds_directory (QDir {installed_data_directory ().absoluteFilePath ("sounds")},
                                     legacy_directory);
#else
  return legacy_directory;
#endif
}

QString voice_manifest_path (QDir const& canonical_directory, QDir const& legacy_directory)
{
  for (auto const& directory : {canonical_directory, legacy_directory})
    {
      QFile manifest {directory.absoluteFilePath ("voices.dat")};
      if (manifest.open (QIODevice::ReadOnly | QIODevice::Text))
        {
          return manifest.fileName ();
        }
    }
  return {};
}

QString app_voice_manifest_path ()
{
  auto const legacy_directory = legacy_app_sounds_directory ();
#if defined (Q_OS_LINUX)
  return voice_manifest_path (QDir {installed_data_directory ().absoluteFilePath ("sounds")},
                              legacy_directory);
#else
  return voice_manifest_path (legacy_directory, legacy_directory);
#endif
}

bool app_sounds_subdirectory_is_safe (QString const& subdirectory)
{
  return normalized_app_sounds_child_is_safe (normalized_app_sounds_child (subdirectory));
}

bool parse_app_voice_entry (QString const& record, QString& subdirectory, QString& display_name)
{
  auto const fields = record.split (QChar {'|'});
  if (fields.size () != 2)
    {
      return false;
    }

  auto const candidate_subdirectory = fields.at (0).trimmed ();
  auto const candidate_display_name = fields.at (1).trimmed ();
  auto const normalized_subdirectory = normalized_app_sounds_child (candidate_subdirectory);
  if (normalized_subdirectory.isEmpty () || normalized_subdirectory == "."
      || candidate_display_name.isEmpty ()
      || !normalized_app_sounds_child_is_safe (normalized_subdirectory))
    {
      return false;
    }

  subdirectory = normalized_subdirectory;
  display_name = candidate_display_name;
  return true;
}

QDir sounds_subdirectory (QDir const& root_directory, QString const& subdirectory)
{
  auto const root = QDir::cleanPath (root_directory.absolutePath ());
  auto const child = normalized_app_sounds_child (subdirectory);

  if (!normalized_app_sounds_child_is_safe (child) || child.isEmpty () || child == ".")
    {
      return QDir {root};
    }

  auto const path = QDir::cleanPath (QDir {root}.absoluteFilePath (child));
  if (path != root && !path.startsWith (root + QChar {'/'}))
    {
      return QDir {root};
    }
  return QDir {path};
}

int next_cyclic_index (int current_index, int item_count)
{
  if (item_count <= 0)
    {
      return -1;
    }

  return (current_index + 1) % item_count;
}

QString writable_file_path (QDir const& writable_dir, QString const& file_name)
{
  return writable_dir.absoluteFilePath (file_name);
}

QString writable_override_or_installed_file_path (QDir const& writable_dir, QDir const& installed_dir,
                                                  QString const& file_name)
{
  auto writable_path = writable_file_path (writable_dir, file_name);
  return QFileInfo::exists (writable_path) ? writable_path : installed_dir.absoluteFilePath (file_name);
}

bool ensure_parent_directory (QString const& file_path)
{
  return QDir {}.mkpath (QFileInfo {file_path}.absolutePath ());
}
