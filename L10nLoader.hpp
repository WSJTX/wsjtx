#ifndef WSJTX_L10N_LOADER_HPP__
#define WSJTX_L10N_LOADER_HPP__

#include <QString>
#include <QStringList>
#include "pimpl_h.hpp"

class QApplication;
class QLocale;

class L10nLoader final
{
public:
  explicit L10nLoader (QApplication *, QLocale const&, QString const& language_override = QString {});
  ~L10nLoader ();

  static QStringList available_languages ();

private:
  class impl;
  pimpl<impl> m_;
};

#endif
