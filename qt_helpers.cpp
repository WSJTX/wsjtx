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

QDateTime qt_round_date_time_to (QDateTime dt, int milliseconds)
{
  dt.setMSecsSinceEpoch (dt.addMSecs (milliseconds / 2).toMSecsSinceEpoch () / milliseconds * milliseconds);
  return dt;
}

QDateTime qt_truncate_date_time_to (QDateTime dt, int milliseconds)
{
  dt.setMSecsSinceEpoch (dt.toMSecsSinceEpoch () / milliseconds * milliseconds);
  return dt;
}

QString app_sounds_directory (QString const& subdirectory)
{
#if defined (__APPLE__)
  QString root {QCoreApplication::applicationDirPath () + "/../Resources/sounds"};
#else
  QString root {QCoreApplication::applicationDirPath () + "/sounds"};
#endif

  auto child = subdirectory;
  while (child.startsWith (QChar {'/'}))
    {
      child.remove (0, 1);
    }

  QDir dir {root};
  auto path = child.isEmpty () ? dir.absolutePath () : dir.absoluteFilePath (child);
  return QDir::cleanPath (path) + QChar {'/'};
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
