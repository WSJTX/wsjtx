#include "FoxLogWindow.hpp"

#include <QApplication>
#include <QAction>
#include <QFile>
#include <QDir>
#include <QSqlTableModel>
#include <QFileDialog>

#include "SettingsGroup.hpp"
#include "Configuration.hpp"
#include "MessageBox.hpp"
#include "models/Bands.hpp"
#include "models/FoxLog.hpp"
#include "item_delegates/ForeignKeyDelegate.hpp"
#include "item_delegates/CallsignDelegate.hpp"
#include "item_delegates/MaidenheadLocatorDelegate.hpp"
#include "item_delegates/SQLiteDateTimeDelegate.hpp"
#include "pimpl_impl.hpp"

#include "ui_FoxLogWindow.h"
#include "moc_FoxLogWindow.cpp"

class FoxLogWindow::impl final
{
public:
  explicit impl (FoxLog * log)
    : log_ {log}
  {
  }

  FoxLog * log_;
  Ui::FoxLogWindow ui_;
};

FoxLogWindow::FoxLogWindow (QSettings * settings, Configuration const * configuration
                            , FoxLog * fox_log, QWidget * parent)
  : AbstractLogWindow {"Fox Log Window", settings, configuration, parent}
  , m_ {fox_log}
{
  setWindowTitle (QApplication::applicationName () + " - Fox Log");
  m_->ui_.setupUi (this);
  m_->ui_.log_table_view->setModel (m_->log_->model ());
  set_log_view (m_->ui_.log_table_view);
  m_->ui_.log_table_view->setItemDelegateForColumn (1, new SQLiteDateTimeDelegate {this});
  m_->ui_.log_table_view->setItemDelegateForColumn (2, new CallsignDelegate {this});
  m_->ui_.log_table_view->setItemDelegateForColumn (3, new MaidenheadLocatorDelegate {this});
  m_->ui_.log_table_view->setItemDelegateForColumn (6, new ForeignKeyDelegate {configuration->bands (), 0, this});
  m_->ui_.log_table_view->horizontalHeader ()->moveSection (6, 1); // move band to first column
  m_->ui_.rate_label->setText ("");
  m_->ui_.queued_label->setNum (0);
  m_->ui_.callers_label->setNum (0);

  // actions
  auto export_action = new QAction {tr ("&Export ADIF ..."), m_->ui_.log_table_view};
  m_->ui_.log_table_view->insertAction (nullptr, export_action);
  connect (export_action, &QAction::triggered, [this, configuration] (bool /*checked*/) {
      auto file_name = QFileDialog::getSaveFileName (this
                                                     , tr ("Export ADIF Log File")
                                                     , configuration->writeable_data_dir ().absolutePath ()
                                                     , tr ("ADIF Log (*.adi)"));
      if (file_name.size () && m_->log_)
        {
          QFile ADIF_file {file_name};
          if (ADIF_file.open (QIODevice::WriteOnly | QIODevice::Text))
            {
              QTextStream output_stream {&ADIF_file};
              m_->log_->export_qsos (output_stream);
            }
          else
            {
              MessageBox::warning_message (this
                                           , tr ("Export ADIF File Error")
                                           , tr ("Cannot open \"%1\" for writing: %2")
                                           .arg (ADIF_file.fileName ()).arg (ADIF_file.errorString ()));
            }
        }
    });

  auto reset_action = new QAction {tr ("&Reset ..."), m_->ui_.log_table_view};
  m_->ui_.log_table_view->insertAction (nullptr, reset_action);
  connect (reset_action, &QAction::triggered, [this, configuration] (bool /*checked*/) {
      if (MessageBox::Yes == MessageBox::query_message( this
                                                        , tr ("Confirm Reset")
                                                        , tr ("Are you sure you want to erase file FoxQSO.txt "
                                                              "and start a new Fox log?")))
        {
          QFile f{configuration->writeable_data_dir ().absoluteFilePath ("FoxQSO.txt")};
          f.remove ();
          Q_EMIT reset_log_model ();
        }
    });
}

FoxLogWindow::~FoxLogWindow ()
{
}

void FoxLogWindow::callers (int n)
{
  m_->ui_.callers_label->setNum (n);
}

void FoxLogWindow::queued (int n)
{
  m_->ui_.queued_label->setNum (n);
}

void FoxLogWindow::rate (int n)
{
  m_->ui_.rate_label->setNum (n);
}

void FoxLogWindow::rate (QString s)
{
  m_->ui_.rate_label->setText(s);
}

void FoxLogWindow::log_model_changed (int row)
{
  if (row >= 0)
    {
      m_->log_->model ()->selectRow (row);
    }
  else
    {
      m_->log_->model ()->select ();
    }
}
