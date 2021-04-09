#ifdef QT5
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include <QApplication>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
