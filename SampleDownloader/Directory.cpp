#include "Directory.hpp"

#include <QVariant>
#include <QString>
#include <QHeaderView>
#include <QStringList>
#include <QFileInfo>
#include <QDir>
#include <QNetworkAccessManager>
#include <QAuthenticator>
#include <QNetworkReply>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonArray>
#include <QJsonObject>

#include "Configuration.hpp"
#include "DirectoryNode.hpp"
#include "EntryName.hpp"
#include "FileNode.hpp"
#include "revision_utils.hpp"
#include "widgets/MessageBox.hpp"

#include "moc_Directory.cpp"

namespace
{
  char const * samples_dir_name = "samples";
  QString const contents_file_name = "contents_" + version (false) + ".json";
}

Directory::Directory (Configuration const * configuration
                      , QNetworkAccessManager * network_manager
                      , QWidget * parent)
  : QTreeWidget {parent}
  , configuration_ {configuration}
  , network_manager_ {network_manager}
  , root_dir_ {configuration_->save_directory ()}
  , contents_ {this
        , network_manager_
        , QDir {root_dir_.absoluteFilePath (samples_dir_name)}.absoluteFilePath (contents_file_name)}
{
  dir_icon_.addPixmap (style ()->standardPixmap (QStyle::SP_DirClosedIcon), QIcon::Normal, QIcon::Off);
  dir_icon_.addPixmap (style ()->standardPixmap (QStyle::SP_DirOpenIcon), QIcon::Normal, QIcon::On);
  file_icon_.addPixmap (style ()->standardPixmap (QStyle::SP_FileIcon));

  setColumnCount (2);
  setHeaderLabels ({tr ("File"), tr ("Progress")});
  headerItem ()->setTextAlignment (0, Qt::AlignHCenter);
  headerItem ()->setTextAlignment (1, Qt::AlignHCenter);
  header ()->setSectionResizeMode (QHeaderView::ResizeToContents);
  setItemDelegate (&item_delegate_);

  connect (network_manager_, &QNetworkAccessManager::authenticationRequired
           , this, &Directory::authentication);
  connect (this, &Directory::itemChanged, [] (QTreeWidgetItem * item) {
      switch (item->type ())
        {
        case FileNode::Type:
          {
            FileNode * node = static_cast<FileNode *> (item);
            if (!node->sync (node->checkState (0) == Qt::Checked))
              {
                FileNode::sync_blocker b {node};
                node->setCheckState (0, node->checkState (0) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
              }
          }
          break;

        default:
          break;
        }
    });
}

bool Directory::url_root (QUrl root)
{
  if (!root.path ().endsWith ('/'))
    {
      root.setPath (root.path () + '/');
    }
  bool valid = root.isValid ();
  if (valid)
    {
      url_root_ = root;
    }
  return valid;
}

void Directory::error (QString const& title, QString const& message)
{
  MessageBox::warning_message (this, title, message);
}

bool Directory::refresh ()
{
  abort ();
  clear ();
  // update locations
  root_dir_ = configuration_->save_directory ();
  QDir contents_dir {root_dir_.absoluteFilePath (samples_dir_name)};
  contents_.local_file_path (contents_dir.absoluteFilePath (contents_file_name));
  auto const relative_path = QDir {samples_dir_name}.filePath (contents_file_name);
  QUrl url {sample_downloader::entry_url (url_root_, relative_path)};
  if (url.isValid ())
    {
      return contents_.sync (url, true, true); // attempt to fetch contents
    }
  else
    {
      MessageBox::warning_message (this
                                   , tr ("URL Error")
                                   , tr ("Invalid URL:\n\"%1\"")
                                   .arg (url.toDisplayString ()));
    }
  return false;
}

void Directory::download_finished (bool success)
{
  if (success)
    {
      QFile contents {contents_.local_file_path ()};
      if (contents.open (QFile::ReadOnly | QFile::Text))
        {
          QJsonParseError json_status;
          auto content = QJsonDocument::fromJson (contents.readAll (), &json_status);
          if (json_status.error)
            {
              MessageBox::warning_message (this
                                           , tr ("JSON Error")
                                           , tr ("Contents file syntax error %1 at character offset %2")
                                           .arg (json_status.errorString ()).arg (json_status.offset));
              return;
            }
          if (!content.isArray ())
            {
              MessageBox::warning_message (this, tr ("JSON Error")
                                           , tr ("Contents file top level must be a JSON array"));
              return;
            }
          QTreeWidgetItem * parent {invisibleRootItem ()};
          parent = new DirectoryNode {parent, samples_dir_name};
          parent->setIcon (0, dir_icon_);
          parent->setExpanded (true);
          auto const rejected = parse_entries (content.array (), QDir {samples_dir_name}, parent);
          if (rejected)
            {
              MessageBox::warning_message (this, tr ("JSON Error")
                                           , tr ("Invalid contents entries ignored: %1").arg (rejected));
            }
        }
      else
        {
          MessageBox::warning_message (this, tr ("File System Error")
                                       , tr ("Failed to open \"%1\"\nError: %2 - %3")
                                       .arg (contents.fileName ())
                                       .arg (contents.error ())
                                       .arg (contents.errorString ()));
        }
    }
}

int Directory::parse_entries (QJsonArray const& entries, QDir const& dir, QTreeWidgetItem * parent)
{
  int rejected {0};
  if (dir.isRelative ()
      && (dir.path () == samples_dir_name
          || dir.path ().startsWith (QString {samples_dir_name} + '/')))
    {
      for (auto const& value: entries)
        {
          if (value.isObject ())
            {
              auto const& entry = value.toObject ();
              auto const& name = entry["name"].toString ();
              auto const relative_path = sample_downloader::relative_entry_path (dir, name);
              if (!relative_path.isEmpty ())
                {
                  auto const& type = entry["type"].toString ();
                  if ("file" == type)
                    {
                      QUrl url {sample_downloader::entry_url (url_root_, relative_path)};
                      if (url.isValid ())
                        {
                          auto node = new FileNode {parent, network_manager_
                                                    , root_dir_.absoluteFilePath (relative_path)
                                                    , url};
                          FileNode::sync_blocker b {node};
                          node->setIcon (0, file_icon_);
                          node->setCheckState (0, node->local () ? Qt::Checked : Qt::Unchecked);
                          update (parent);
                        }
                      else
                        {
                          ++rejected;
                        }
                    }
                  else if ("directory" == type)
                    {
                      auto const& child_entries = entry["entries"];
                      if (child_entries.isArray ())
                        {
                          auto node = new DirectoryNode {parent, name};
                          node->setIcon (0, dir_icon_);
                          rejected += parse_entries (child_entries.toArray ()
                                                     , QDir {relative_path}, node);
                        }
                      else
                        {
                          ++rejected;
                        }
                    }
                  else
                    {
                      ++rejected;
                    }
                }
              else
                {
                  ++rejected;
                }
            }
          else
            {
              ++rejected;
            }
        }
    }
  else
    {
      rejected += entries.size ();
    }
  return rejected;
}

void Directory::abort ()
{
  QTreeWidgetItemIterator iter {this};
  while (*iter)
    {
      if ((*iter)->type () == FileNode::Type)
        {
          auto * node = static_cast<FileNode *> (*iter);
          node->abort ();
        }
      ++iter;
    }
}

namespace
{
  //
  // traverse the passed subtree accumulating the number of items, the
  // number we have size data for, the bytes downloaded so far and the
  // maximum bytes to expect
  //
  int recurse_children (QTreeWidgetItem const * item, int * counted
                        , qint64 * bytes, qint64 * max)
  {
    int items {0};
    for (int index {0}; index < item->childCount (); ++index)
      {
        auto const * child = item->child (index);
        if (child->type () == FileNode::Type)  // only count files
          {
            ++items;
            if (auto size = child->data (1, Qt::UserRole).toLongLong ())
              {
                *max += size;
                ++*counted;
              }
            *bytes += child->data (1, Qt::DisplayRole).toLongLong ();
          }
        else
          {
            // recurse into sub-directory subtrees
            items += recurse_children (child, counted, bytes, max);
          }
      }
    return items;
  }
}

void Directory::update (QTreeWidgetItem * item)
{
  // iterate the tree under item and accumulate the progress
  if (item)
    {
      Q_ASSERT (item->type () == DirectoryNode::Type);
      qint64 max {0};
      qint64 bytes {0};
      int counted {0};

      // get the count, progress and size of children
      // int items {recurse_children (item, &counted, &bytes, &max)};
      recurse_children (item, &counted, &bytes, &max);

      // estimate size of items not yet downloaded as average of
      // those actually present
      // if (counted)
      //   {
      //     max += (items - counted) * max / counted;
      //   }

      // save as our progress
      item->setData (1, Qt::UserRole, max);
      item->setData (1, Qt::DisplayRole, bytes);

      // recurse up to top
      update (item->parent ());
    }
}

void Directory::authentication (QNetworkReply * /* reply */
                                , QAuthenticator * /* authenticator */)
{
  MessageBox::warning_message (this, tr ("Network Error"), tr ("Authentication required"));
}
