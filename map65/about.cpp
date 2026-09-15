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
    "for the JT65 and Q65 protocols, with a matching transmitting <br />"
    "facility. It is primarily intended for amateur radio EME communication. <br /><br />"
    "Copyright 2001-2026 by Joe Taylor, K1JT, and the WSJT <br/>"
    "Develolpment Group.<br /><br />"
    "MAP65 is licensed under the terms of Version 3 <br />"
    "of the GNU General Public License (GPL) <br /><br />"
    "<a href=" TO_STRING__ (PROJECT_HOMEPAGE) ">"
    "<img src=\":/icon_128x128.png\" /></a>"
    "<a href=\"https://www.gnu.org/licenses/gpl-3.0.txt\">"
    "<img src=\":/gpl-v3-logo.svg\" height=\"80\" /><br />"
    "https://www.gnu.org/licenses/gpl-3.0.txt</a>");
}

CAboutDlg::~CAboutDlg()
{
  delete ui;
}
