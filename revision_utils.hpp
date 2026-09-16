#ifndef REVISION_UTILS_HPP__
#define REVISION_UTILS_HPP__

#include <QString>

QString revision (QString const& svn_rev_string = QString {});
QString display_revision ();
QString version (bool include_patch = true);
QString program_title (QString const& revision = QString {});
// Same as program_title(), plus a ™ mark -- only for the three main-window
// titles (widgets/map65/qmap mainwindow.cpp), not the general-purpose
// program_title() itself, which also feeds non-UI uses (WAV metadata,
// startup log, generic dialog titles) where a trademark symbol doesn't belong.
QString branded_program_title (QString const& revision = QString {});
QString http_user_agent ();
QString copyright_notice_text ();

#endif
