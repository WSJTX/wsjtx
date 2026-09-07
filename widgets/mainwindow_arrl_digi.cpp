#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "activeStations.h"
#include "ActiveStationList.hpp"
#include "Decoder/decodedtext.h"
#include "models/Bands.hpp"
#include <QRegularExpression>
#include <QMutableMapIterator>
#include <cmath>
#include "commons.h"

extern "C" {
  void azdist_(char* MyGrid, char* HisGrid, double* utch, int* nAz, int* nEl,
                int* nDmiles, int* nDkm, int* nHotAz, int* nHotABetter, fortran_charlen_t n1, fortran_charlen_t n2);
}

void MainWindow::ARRL_Digi_Update(DecodedText dt)
{
  if(m_mode=="Q65") {
    m_fetched=0;
    readWidebandDecodes();
    return;
  }

  // Extract information relevant for the ARRL Digi contest
  QString deCall;
  QString deGrid;
  dt.deCallAndGrid(/*out*/deCall,deGrid);
  ActiveCall ac;
  RecentCall rc;

  if(deGrid.contains(MainWindow::grid_regexp)) {
     if(!m_activeCall.contains(deCall) or deGrid!=m_activeCall.value(deCall).grid4) {
       // Transmitting station's call is not already in QMap "m_activeCall", or grid has changed.
       // Insert the call, grid, and associated fixed data into the list.

       double utch=0.0;
       int nAz,nEl,nDmiles,nDkm,nHotAz,nHotABetter;
       QString my_Grid = m_config.my_grid();
       if (my_Grid.length() < 5) my_Grid = m_config.my_grid().left(4)+"mm";
       QString de_Grid= deGrid.left(4)+"mm";
       azdist_(const_cast <char *> (my_Grid.toLatin1().constData()),
               const_cast <char *> (de_Grid.toLatin1().constData()),&utch,
               &nAz,&nEl,&nDmiles,&nDkm,&nHotAz,&nHotABetter,(fortran_charlen_t)6,(fortran_charlen_t)6);
       int points=nDkm/500;
       if(nDkm > 500*points) points += 1;
       points += 1;
       ac.grid4=deGrid;
       ac.bands=".......";
       ac.az=nAz;
       ac.points=points;
       m_activeCall[deCall]=ac;
     }
  }

  m_points=-1;
  if(m_activeCall.contains(deCall)) {

// Don't display stations we already worked on this band.
    QString band=m_config.bands()->find(m_operatingFrequency.rx ());
    if(band=="160m" and m_activeCall[deCall].bands.indexOf("a")>=0) {m_recentCall.remove(deCall); return;}
    if(band=="80m"  and m_activeCall[deCall].bands.indexOf("b")>=0) {m_recentCall.remove(deCall); return;}
    if(band=="40m"  and m_activeCall[deCall].bands.indexOf("c")>=0) {m_recentCall.remove(deCall); return;}
    if(band=="20m"  and m_activeCall[deCall].bands.indexOf("d")>=0) {m_recentCall.remove(deCall); return;}
    if(band=="15m"  and m_activeCall[deCall].bands.indexOf("e")>=0) {m_recentCall.remove(deCall); return;}
    if(band=="10m"  and m_activeCall[deCall].bands.indexOf("f")>=0) {m_recentCall.remove(deCall); return;}
    if(band=="6m"   and m_activeCall[deCall].bands.indexOf("g")>=0) {m_recentCall.remove(deCall); return;}

    // Update the variable data for this deCall
    rc.dialFreq=m_operatingFrequency.rx ();
    rc.audioFreq=dt.frequencyOffset();
    rc.snr=dt.snr();
    m_latestDecodeTime=dt.timeInSeconds();
    rc.txEven = (m_latestDecodeTime % int(2*m_TRperiod)) > 0;
    rc.ready2call=false;
    auto const words = dt.messageWords();
    bool bCQ=!words.isEmpty() && words.at(0).left(3)=="CQ ";
    if(bCQ or deGrid=="RR73" or deGrid=="73") rc.ready2call=true;
    rc.decodeTime=m_latestDecodeTime;
    m_recentCall[deCall]=rc;
    m_points=m_activeCall.value(deCall).points;
  }
  updateRate();
}

void MainWindow::ARRL_Digi_Display()
{
  if(m_mode=="Q65") {
    m_fetched=0;
    readWidebandDecodes();
    return;
  }
  if (m_mode == "FT8" && m_specOp == SpecOp::FOX) {
    if (m_ActiveStationsWidget != NULL) {
      m_ActiveStationsWidget->setClickOK(true);
    }
    return;
  }
  QMutableMapIterator<QString,RecentCall> icall(m_recentCall);
  QString deCall,deGrid;
  int age=0;
  int maxAge=m_ActiveStationsWidget->maxAge();
  int points=0;
  QVector<ActiveStationListItem> rows;

  while (icall.hasNext()) {
    icall.next();
    deCall=icall.key();
    age=int((m_latestDecodeTime - icall.value().decodeTime)/m_TRperiod + 0.5);
    if(age<0) age=age + int(86400/m_TRperiod);
    int itx=1;
    if(icall.value().txEven) itx=0;
    int snr=icall.value().snr;
    int freq=icall.value().audioFreq;
    if(age>maxAge) {
      icall.remove();
    } else {
      bool bReady=false;
      if(age==0 and m_recentCall.value(deCall).ready2call) bReady=true;

      QString bands=m_activeCall[deCall].bands;
      bool bWorkedOnBand=false;
      if(m_currentBand=="160m" and bands.mid(0,1)!=".") bWorkedOnBand=true;
      if(m_currentBand=="80m"  and bands.mid(1,1)!=".") bWorkedOnBand=true;
      if(m_currentBand=="40m"  and bands.mid(2,1)!=".") bWorkedOnBand=true;
      if(m_currentBand=="20m"  and bands.mid(3,1)!=".") bWorkedOnBand=true;
      if(m_currentBand=="15m"  and bands.mid(4,1)!=".") bWorkedOnBand=true;
      if(m_currentBand=="10m"  and bands.mid(5,1)!=".") bWorkedOnBand=true;
      if(m_currentBand=="6m"   and bands.mid(6,1)!=".") bWorkedOnBand=true;

      if((bReady or !m_ActiveStationsWidget->readyOnly()) and !bWorkedOnBand) {
        int az=m_activeCall[deCall].az;
        deGrid=m_activeCall[deCall].grid4;
        points=m_activeCall[deCall].points;
        float x=float(age)/(maxAge+1);
        if(x>1.0) x=0;
        QString t1;
        if(!bReady) t1 = t1.asprintf("  %3d  %+2.2d  %4d  %1d %2d %4d",az,snr,freq,itx,age,points);
        if(bReady)  t1 = t1.asprintf("  %3d  %+2.2d  %4d  %1d %2d*%4d",az,snr,freq,itx,age,points);
//        t1 = (deCall + "   ").left(6) + "  " + m_activeCall[deCall].grid4 + t1 + "  " + bands;
        t1 = (deCall + "   ").left(6) + "  " + m_activeCall[deCall].grid4 + t1;
        rows.append({points - x, t1});
      }
    }
  }
  if(rows.isEmpty()) return;
  m_ActiveStationsWidget->setClickOK(false);
  int maxRecent=qMin(m_ActiveStationsWidget->maxRecent(), MaxActiveStationRows);
  rows=sorted_limited_active_station_items(rows, maxRecent, true);
  std::fill(m_ready2call.begin(), m_ready2call.end(), QString {});
  QString t;
  for(int i=0; i<rows.size(); i++) {
    m_ready2call[i]=rows[i].text;
    QString t1=QString::number(i + 1) + ".  ";
    if(i + 1<10) t1=" " + t1;
    t += (t1 + rows[i].text + "\n");
  }
  if(m_ActiveStationsWidget!=NULL) m_ActiveStationsWidget->displayRecentStations(ActiveStations::DisplayMode::Standard,t);
  m_ActiveStationsWidget->setClickOK(true);
}
