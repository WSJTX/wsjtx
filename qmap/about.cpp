#include "about.h"
#include "revision_utils.hpp"
#include "ui_about.h"

CAboutDlg::CAboutDlg(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::CAboutDlg)
{
  ui->setupUi(this);
  ui->labelTxt->setText("<html><h2>" + QString {"QMAP v"
                + QCoreApplication::applicationVersion ()+ " "
                + revision ()}.simplified () + " improved PLUS" + "</h2>"
    "QMAP is a wideband receiver for the Q65 protocol, intended<br/>"
    "primarily for amateur radio EME communication.  It works<br/>"
    "in close cooperation with WSJT-X or WSJT-X Improved,<br/>"
    "versions 2.7 and later.<br/><br />"
    "Copyright 2001-2025 by Joe Taylor, K1JT.<br/>"
    "Improved PLUS edition by DG2YCB et al., (c) 2020-2025.");
}

CAboutDlg::~CAboutDlg()
{
  delete ui;
}
