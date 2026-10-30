#ifndef SAMPLE_DOWNLOADER_ENTRY_NAME_HPP__
#define SAMPLE_DOWNLOADER_ENTRY_NAME_HPP__

#include <QDir>
#include <QString>
#include <QUrl>

namespace sample_downloader
{
  inline bool valid_entry_name (QString const& name)
  {
    if (name.isEmpty () || name == "." || name == ".."
        || name.endsWith ('.') || name.endsWith (' '))
      {
        return false;
      }

    auto const reserved_characters = QStringLiteral ("<>:\"/\\|?*;");
    for (auto const character: name)
      {
        auto const value = character.unicode ();
        if (value < 0x20 || value == 0x7f || reserved_characters.contains (character)) return false;
      }

    auto const device_name = name.section ('.', 0, 0).trimmed ().toUpper ();
    if (device_name == "CON" || device_name == "PRN"
        || device_name == "AUX" || device_name == "NUL")
      {
        return false;
      }
    if (device_name.size () == 4
        && (device_name.startsWith ("COM") || device_name.startsWith ("LPT"))
        && QStringLiteral ("123456789\u00b9\u00b2\u00b3").contains (device_name[3]))
      {
        return false;
      }
    return true;
  }

  inline QString relative_entry_path (QDir const& directory, QString const& name)
  {
    if (!valid_entry_name (name)) return {};

    auto const path = QDir::cleanPath (directory.filePath (name));
    auto const samples_prefix = QStringLiteral ("samples/");
    if (QDir::isAbsolutePath (path) || !path.startsWith (samples_prefix)) return {};
    return path;
  }

  inline QUrl entry_url (QUrl const& root, QString const& relative_path)
  {
    QUrl relative;
    relative.setPath (relative_path, QUrl::DecodedMode);
    return root.resolved (relative);
  }
}

#endif
