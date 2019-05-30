#ifndef QT_DB_HELPERS_HPP_
#define QT_DB_HELPERS_HPP_

#include <utility>
#include <stdexcept>
#include <QString>
#include <QSqlError>
#include <QSqlTableModel>
#include "boost/core/noncopyable.hpp"

template<typename T, typename Func, typename... Args>
void SQL_error_check (T&& object, Func func, Args&&... args)
{
  if (!(std::forward<T> (object).*func) (std::forward<Args> (args)...))
    {
      auto error = object.lastError ();
      if (QSqlError::NoError != error.type ())
        {
          throw std::runtime_error {("Database Error: " + error.text ()).toStdString ()};
        }
    }
}

class ConditionalTransaction final
  : private boost::noncopyable
{
public:
  explicit ConditionalTransaction (QSqlTableModel& model)
    : model_ (model)
    , submitted_ {false}
  {
    model_.database ().transaction ();
  }

  bool submit (bool throw_on_error = true)
  {
    bool ok {true};
    if (throw_on_error)
      {
        SQL_error_check (model_
                         , QSqlTableModel::OnManualSubmit == model_.editStrategy ()
                         ? &QSqlTableModel::submitAll
                         : &QSqlTableModel::submit);
      }
    else
      {
        ok = QSqlTableModel::OnManualSubmit == model_.editStrategy ()
          ? model_.submitAll () : model_.submit ();
      }
    submitted_ = submitted_ || ok;
    return ok;
  }

  void revert ()
  {
    if (QSqlTableModel::OnManualSubmit == model_.editStrategy ())
      {
        model_.revertAll ();
      }
    else
      {
        model_.revert ();
      }
  }

  ~ConditionalTransaction ()
  {
    if (model_.isDirty ())
      {
        // abandon un-submitted changes to the model
        if (QSqlTableModel::OnManualSubmit == model_.editStrategy ())
          {
            model_.revertAll ();
          }
        else
          {
            model_.revert ();
          }
      }
    auto database = model_.database ();
    if (submitted_)
      {
        SQL_error_check (database, &QSqlDatabase::commit);
      }
    else
      {
        database.rollback ();
      }
  }

private:
  QSqlTableModel& model_;
  bool submitted_;
};

#endif
