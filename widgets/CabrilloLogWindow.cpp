#include "CabrilloLogWindow.hpp"

#include <stdexcept>
#include <QApplication>
#include <QIdentityProxyModel>
#include <QSqlTableModel>
#include "Configuration.hpp"
#include "models/Bands.hpp"
#include "item_delegates/FrequencyDelegate.hpp"
#include "item_delegates/ForeignKeyDelegate.hpp"
#include "item_delegates/CallsignDelegate.hpp"
#include "pimpl_impl.hpp"

#include "ui_CabrilloLogWindow.h"

namespace
{
  class FormatProxyModel final
    : public QIdentityProxyModel
  {
  public:
    explicit FormatProxyModel (QObject * parent = nullptr)
      : QIdentityProxyModel {parent}
    {
    }

    QVariant data (QModelIndex const& index, int role) const override
    {
      if (Qt::TextAlignmentRole == role && index.isValid ())
        {
          switch (index.column ())
            {
            case 1:
            case 7:
              return Qt::AlignRight + Qt::AlignVCenter;
            default:
              break;
            }
        }
      return QIdentityProxyModel::data (index, role);
    }
  };
}

class CabrilloLogWindow::impl final
{
public:
  explicit impl (QSqlTableModel * log_model)
    : log_model_ {log_model}
  {
  }

  QSqlTableModel * log_model_;
  FormatProxyModel format_model_;
  Ui::CabrilloLogWindow ui_;
};

CabrilloLogWindow::CabrilloLogWindow (QSettings * settings, Configuration const * configuration
                            , QSqlTableModel * cabrillo_log_model, QWidget * parent)
  : AbstractLogWindow {"Cabrillo Log Window", settings, configuration, parent}
  , m_{cabrillo_log_model}
{
  setWindowTitle (QApplication::applicationName () + " - Cabrillo Log");
  m_->ui_.setupUi (this);
  m_->format_model_.setSourceModel (m_->log_model_);
  m_->ui_.log_table_view->setModel (&m_->format_model_);
  set_log_view (m_->ui_.log_table_view);
  m_->ui_.log_table_view->setItemDelegateForColumn (1, new FrequencyDelegate {this});
  m_->ui_.log_table_view->setItemDelegateForColumn (4, new CallsignDelegate {this});
  auto h_header = m_->ui_.log_table_view->horizontalHeader ();
  h_header->moveSection (7, 1); // band to first column
}

CabrilloLogWindow::~CabrilloLogWindow ()
{
}

void CabrilloLogWindow::log_model_changed (int row)
{
  if (row >= 0)
    {
      m_->log_model_->selectRow (row);
    }
  else
    {
      m_->log_model_->select ();
    }
}
