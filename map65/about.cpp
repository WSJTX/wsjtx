#include "about.h"
#include "ui_about.h"

CAboutDlg::CAboutDlg(QWidget *parent, QString Revision) :
  QDialog(parent),
  m_Revision(Revision),
  ui(new Ui::CAboutDlg)
{
  ui->setupUi(this);
  ui->labelTxt->clear();
  m_Str  = "<html><h2>" + m_Revision + "</h2>\n\n";
  m_Str += "MAP65 implements a wideband polarization-matching receiver <br>";
  m_Str += "for the JT65 protocol, with a matching transmitting facility. <br>";
  m_Str += "It is primarily intended for amateur radio EME communication. <br><br>";
  m_Str += "Copyright 2001-2021 by Joe Taylor, K1JT.   Additional <br>";
  m_Str += "acknowledgments are contained in the source code. <br>";
  ui->labelTxt->setText(m_Str);
}

CAboutDlg::~CAboutDlg()
{
  delete ui;
}
