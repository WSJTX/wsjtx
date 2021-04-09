#include "messages.h"
#include "ui_messages.h"
#include "mainwindow.h"

Messages::Messages(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::Messages)
{
  ui->setupUi(this);
  ui->messagesTextBrowser->setStyleSheet( \
          "QTextBrowser { background-color : #000066; color : red; }");
  ui->messagesTextBrowser->clear();
  m_cqOnly=false;
  m_cqStarOnly=false;
  connect(ui->messagesTextBrowser,SIGNAL(selectCallsign(bool)),this,
          SLOT(selectCallsign2(bool)));
}

Messages::~Messages()
{
  delete ui;
}

void Messages::setText(QString t, QString t2)
{
  QString cfreq,cfreq0;
  m_t=t;
  m_t2=t2;

  QString s="QTextBrowser{background-color: "+m_colorBackground+"}";
  ui->messagesTextBrowser->setStyleSheet(s);

  ui->messagesTextBrowser->clear();
  QStringList lines = t.split( "\n", QString::SkipEmptyParts );
  foreach( QString line, lines ) {
    QString t1=line.mid(0,50);
    int ncq=t1.indexOf(" CQ ");
    if((m_cqOnly or m_cqStarOnly) and  ncq< 0) continue;
    if(m_cqStarOnly) {
      QString caller=t1.mid(ncq+4,-1);
      int nz=caller.indexOf(" ");
      caller=caller.mid(0,nz);
      int i=t2.indexOf(caller);
      if(t2.mid(i-1,1)==" ") continue;
    }
    int n=line.mid(50,2).toInt();
    if(n==0) ui->messagesTextBrowser->setTextColor(m_color0);
    if(n==1) ui->messagesTextBrowser->setTextColor(m_color1);
    if(n==2) ui->messagesTextBrowser->setTextColor(m_color2);
    if(n>=3) ui->messagesTextBrowser->setTextColor(m_color3);
    cfreq=t1.mid(0,3);
    if(cfreq == cfreq0) {
      t1="   " + t1.mid(3,-1);
    }
    cfreq0=cfreq;
    ui->messagesTextBrowser->append(t1);
  }
}

void Messages::selectCallsign2(bool ctrl)
{
  QString t = ui->messagesTextBrowser->toPlainText();   //Full contents
  int i=ui->messagesTextBrowser->textCursor().position();
  int i0=t.lastIndexOf(" ",i);
  int i1=t.indexOf(" ",i);
  QString hiscall=t.mid(i0+1,i1-i0-1);
  if(hiscall!="") {
    if(hiscall.length() < 13) {
      QString t1 = t.mid(0,i);              //contents up to text cursor
      int i1=t1.lastIndexOf("\n") + 1;
      QString t2 = t1.mid(i1,i-i1);         //selected line
      emit click2OnCallsign(hiscall,t2);
    }
  }
}

void Messages::setColors(QString t)
{
  m_colorBackground = "#"+t.mid(0,6);
  m_color0 = "#"+t.mid(6,6);
  m_color1 = "#"+t.mid(12,6);
  m_color2 = "#"+t.mid(18,6);
  m_color3 = "#"+t.mid(24,6);
  setText(m_t,m_t2);
}

void Messages::on_cbCQ_toggled(bool checked)
{
  m_cqOnly = checked;
  setText(m_t,m_t2);
}

void Messages::on_cbCQstar_toggled(bool checked)
{
  m_cqStarOnly = checked;
  setText(m_t,m_t2);
}
