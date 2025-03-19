// -*- Mode: C++ -*-
#ifndef ITONEANDICW_H
#define ITONEANDICW_H

#define MAX_NUM_SYMBOLS 250
#define NUM_CW_SYMBOLS 250

extern int volatile itone[MAX_NUM_SYMBOLS];   //Audio tones for all Tx symbols
extern int volatile icw[NUM_CW_SYMBOLS];	    //Dits for CW ID

//--------------------------------------------------------------- MainWindow
namespace Ui {
  class MainWindow2;
}


class MainWindow2
   : public QObject
{
  Q_OBJECT;

};
#endif // MAINWINDOW_H2
