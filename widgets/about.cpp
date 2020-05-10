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
                             + " " + revision ()}.simplified () + "</h2><br />"
                         "WSJT-X implements a number of digital modes designed for <br />"
                         "weak-signal Amateur Radio communication.  <br /><br />"
                         "&copy; 2001-2020 by Joe Taylor, K1JT, Bill Somerville, G4WJS, <br />"
                         "and Steve Franke, K9AN.  <br /><br />"
                         "We gratefully acknowledge contributions from AC6SL, AE4JY, DJ0OT, <br />"
                         "G3WDG, G4KLA, IV3NWV, IW3RAB, K3WYC, KA6MAL, KA9Q, KB1ZMX,<br />"
                         "KD6EKQ, KI7MT, KK1D, ND0B, PY2SDR, VE1SKY, VK3ACF, VK4BDJ,<br />"
                         "VK7MO, W4TI, W4TV, and W9MDB.<br /><br />"
                         "WSJT-X is licensed under the terms of Version 3 <br />"
                         "of the GNU General Public License (GPL) <br /><br />"
                         "<a href=" WSJTX_STRINGIZE (PROJECT_HOMEPAGE) ">"
                         "<img src=\":/icon_128x128.png\" /></a>"
                         "<a href=\"https://www.gnu.org/licenses/gpl-3.0.txt\">"
                         "<img src=\":/gpl-v3-logo.svg\" height=\"80\" /><br />"
                         "https://www.gnu.org/licenses/gpl-3.0.txt</a>");
}

CAboutDlg::~CAboutDlg()
{
}
