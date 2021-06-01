#ifndef GETFILE_H
#define GETFILE_H
#include <QString>
#include <QFile>
#include <QDebug>
#include "commons.h"

void getfile(QString fname, bool xpol, int dbDgrd);
void savetf2(QString fname, bool xpol);
float gran();

#endif // GETFILE_H
