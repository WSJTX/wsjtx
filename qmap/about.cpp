#include "about.h"
#include "revision_utils.hpp"
#include "ui_about.h"

CAboutDlg::CAboutDlg(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::CAboutDlg)
{
  ui->setupUi(this);
  ui->labelTxt->setText("<html><h2>" + QString {"QMAP v"
                + QCoreApplication::applicationVersion ()
                + " " + revision ()}.simplified () + "</h2><br />"
    "QMAP is a wideband receiver for the Q65 protocol, intnded<br />"
    "primarily for amateur radio EME communication.  It works <br />"
    "in close cooperation with WSJT-X, versions 2.7 and later. <br /><br />"
    "Copyright 2001-2024 by Joe Taylor, K1JT.   Additional <br />"
    "acknowledgments are contained in the source code.");
}

CAboutDlg::~CAboutDlg()
{
  delete ui;
}
