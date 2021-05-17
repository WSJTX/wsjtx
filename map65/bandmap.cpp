#include "bandmap.h"
#include <QSettings>
#include "ui_bandmap.h"
#include "qt_helpers.hpp"
#include "SettingsGroup.hpp"
#include <QDebug>

BandMap::BandMap (QString const& settings_filename, QWidget * parent)
  : QWidget {parent},
    ui {new Ui::BandMap},
    m_settings_filename {settings_filename}
{
  ui->setupUi (this);
  setWindowTitle ("Band Map");
  setWindowFlags (Qt::Dialog | Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint);
  QSettings settings {m_settings_filename, QSettings::IniFormat};
  SettingsGroup g {&settings, "MainWindow"}; // MainWindow group for
                                             // historical reasons
  setGeometry (settings.value ("BandMapGeom", QRect {280, 400, 142, 400}).toRect ());
  ui->bmTextBrowser->setStyleSheet(
                                   "QTextBrowser { background-color : #000066; color : red; }");
}

BandMap::~BandMap ()
{
  QSettings settings {m_settings_filename, QSettings::IniFormat};
  SettingsGroup g {&settings, "MainWindow"};
  settings.setValue ("BandMapGeom", geometry ());
  delete ui;
}

void BandMap::setText(QString t)
{
  m_bandMapText=t;
  int w=ui->bmTextBrowser->size().width();
  int ncols=1;
  if(w>220) ncols=2;
  QString s="QTextBrowser{background-color: "+m_colorBackground+"}";
  ui->bmTextBrowser->setStyleSheet(s);
  QString t0="<html style=\" font-family:'Courier New';"
      "font-size:9pt; background-color:#000066\">"
      "<table border=0 cellspacing=7><tr><td>\n";
  QString tfreq,tspace,tcall;
  QString s0,s1,s2,s3,bg;
  bg="<span style=color:"+m_colorBackground+";>.</span>";
  s0="<span style=color:"+m_color0+";>";
  s1="<span style=color:"+m_color1+";>";
  s2="<span style=color:"+m_color2+";>";
  s3="<span style=color:"+m_color3+";>";

  ui->bmTextBrowser->clear();
  QStringList lines = t.split( "\n", SkipEmptyParts );
  int nrows=(lines.length()+ncols-1)/ncols;

  for(int i=0; i<nrows; i++) {
    tfreq=lines[i].mid(0,3);
    tspace=lines[i].mid(4,1);
    if(tspace==" ") tspace=bg;
    tcall=lines[i].mid(5,7);
    int n=lines[i].mid(13,1).toInt();
    if(n==0) t0 += s0;
    if(n==1) t0 += s1;
    if(n==2) t0 += s2;
    if(n>=3) t0 += s3;
    t0 += (tfreq + tspace + tcall + "</span><br>\n");
  }

  if(ncols==2) {                                  //2-column display
    t0 += "<td><br><td>\n";
    for(int i=nrows; i<lines.length(); i++) {
      tfreq=lines[i].mid(0,3);
      tspace=lines[i].mid(4,1);
      if(tspace=="  ") tspace=bg;
      tcall=lines[i].mid(5,7);
      int n=lines[i].mid(13,1).toInt();
      if(n==0) t0 += s0;
      if(n==1) t0 += s1;
      if(n==2) t0 += s2;
      if(n>=3) t0 += s3;
      t0 += (tfreq + tspace + tcall + "</span><br>\n");
    }
    if(2*nrows>lines.length()) t0 += (s0 + "</span><br>\n");
  }
  ui->bmTextBrowser->setHtml(t0);
}

void BandMap::resizeEvent(QResizeEvent* )
{
  setText(m_bandMapText);
}

void BandMap::setColors(QString t)
{
  m_colorBackground = "#"+t.mid(0,6);
  m_color0 = "#"+t.mid(6,6);
  m_color1 = "#"+t.mid(12,6);
  m_color2 = "#"+t.mid(18,6);
  m_color3 = "#"+t.mid(24,6);
  setText(m_bandMapText);
}
