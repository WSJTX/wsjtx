#include "qt_helpers.hpp"

#include <QString>
#include <QFont>
#include <QWidget>
#include <QStyle>
#include <QVariant>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <limits>

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
QString app_sounds_root ()
{
#if defined (__APPLE__)
  return QCoreApplication::applicationDirPath () + "/../Resources/sounds";
#else
  return QCoreApplication::applicationDirPath () + "/sounds";
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

QString app_sounds_directory (QString const& subdirectory)
{
  auto const root = QDir::cleanPath (QDir {app_sounds_root ()}.absolutePath ());
  auto const child = normalized_app_sounds_child (subdirectory);

  if (!normalized_app_sounds_child_is_safe (child) || child.isEmpty () || child == ".")
    {
      return root + QChar {'/'};
    }

  auto const path = QDir::cleanPath (QDir {root}.absoluteFilePath (child));
  if (path != root && !path.startsWith (root + QChar {'/'}))
    {
      return root + QChar {'/'};
    }
  return path + QChar {'/'};
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
