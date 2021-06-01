#include "about.h"
#include "revision_utils.hpp"
#include "ui_about.h"

CAboutDlg::CAboutDlg(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::CAboutDlg)
{
  ui->setupUi(this);
  ui->labelTxt->setText("<html><h2>" + QString {"MAP65 v"
                + QCoreApplication::applicationVersion ()
                + " " + revision ()}.simplified () + "</h2><br />"
    "MAP65 implements a wideband polarization-matching receiver <br />"
    "for the JT65 protocol, with a matching transmitting facility. <br />"
    "It is primarily intended for amateur radio EME communication. <br /><br />"
    "Copyright 2001-2021 by Joe Taylor, K1JT.   Additional <br />"
    "acknowledgments are contained in the source code.");
}

CAboutDlg::~CAboutDlg()
{
  delete ui;
}
