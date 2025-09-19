#include "about.h"

#include <QCoreApplication>
#include <QString>

#include "revision_utils.hpp"

#include "ui_about.h"

CAboutDlg::CAboutDlg(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::CAboutDlg)
{
  ui->setupUi(this);

  ui->labelTxt->setText ("<h2>" + QString {"WSJT-X v"
                                             + QCoreApplication::applicationVersion ()
                                             + " " + revision ()}.simplified () + "</h2>"

    "WSJT-X implements a number of digital modes designed for <br />"
    "weak-signal Amateur Radio communication.  <br /><br />"
    "&copy; 2001-2025 by Joe Taylor, K1JT, Bill Somerville, G4WJS, <br />"
    "Steve Franke, K9AN, Nico Palermo, IV3NWV, <br />"
    "Uwe Risse, DG2YCB, Brian Moran, N9ADG, <br />"
    "and Roger Rehr, W3SZ.<br /><br />"
    "We gratefully acknowledge contributions from AC6SL, AE4JY,<br />"
    "DF2ET, DJ0OT, DL3WDG, EA4AC, G4KLA, IW3RAB, JA7UDE,<br />"
    "K3WYC, KA1GT, KA6MAL, KA9Q, KB1ZMX, KD6EKQ, KG4IYS, KI7MT,<br />"
    "KK1D, ND0B, PY1ZRJ, PY2SDR, VE1SKY, VK3ACF, VK4BDJ,<br />"
    "VK7MO, VR2UPU, W3DJS, W4TI, W4TV, and W9MDB.<br /><br />"
    "WSJT-X is licensed under the terms of Version 3 <br />"
    "of the GNU General Public License (GPL) <br /><br />"
    "<a href=" TO_STRING__ (PROJECT_HOMEPAGE) ">"
    "<img src=\":/icon_128x128.png\" /></a>"
    "<a href=\"https://www.gnu.org/licenses/gpl-3.0.txt\">"
    "<img src=\":/gpl-v3-logo.svg\" height=\"80\" /><br />"
    "https://www.gnu.org/licenses/gpl-3.0.txt</a>");
}

CAboutDlg::~CAboutDlg()
{
}
