#ifndef GETFILE_H
#define GETFILE_H
#include <QString>
#include <QFile>
#include <QDebug>
#include "commons.h"

// Sample rate (Hz) of an .iq file (60 s or older 56 s recording), deduced from its size, or 0 if
// the size fits no supported rate.
int iqFileRate(QString fname);
void getfile(QString fname, int dbDgrd);
void save_iq(QString fname);
float gran();

#endif // GETFILE_H
