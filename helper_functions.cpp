#include "helper_functions.h"

double tx_duration(QString mode, double trPeriod, int nsps, bool bFast9)
{
  double txt=0.0;
  if(mode=="FT4")  txt=1.0 + 105*576/12000.0;      // FT4
  if(mode=="FT8")  txt=1.0 + 79*1920/12000.0;      // FT8
  if(mode=="JT4")  txt=1.0 + 207.0*2520/11025.0;   // JT4
  if(mode=="JT9")  txt=1.0 + 85.0*nsps/12000.0;  // JT9
  if(mode=="JT65") txt=1.0 + 126*4096/11025.0;     // JT65
  if(mode=="Q65")  {                                      // Q65
    if(trPeriod==15) txt=0.5 + 85*1800/12000.0;
    if(trPeriod==30) txt=0.5 + 85*3600/12000.0;
    if(trPeriod==60) txt=1.0 + 85*7200/12000.0;
    if(trPeriod==120) txt=1.0 + 85*16000/12000.0;
    if(trPeriod==300) txt=1.0 + 85*41472/12000.0;
  }
  if(mode=="WSPR") txt=2.0 + 162*8192/12000.0;     // WSPR
  if(mode=="FST4" or mode=="FST4W") {               //FST4, FST4W
    if(trPeriod==15)  txt=1.0 + 160*720/12000.0;
    if(trPeriod==30)  txt=1.0 + 160*1680/12000.0;
    if(trPeriod==60)  txt=1.0 + 160*3888/12000.0;
    if(trPeriod==120) txt=1.0 + 160*8200/12000.0;
    if(trPeriod==300) txt=1.0 + 160*21504/12000.0;
    if(trPeriod==900) txt=1.0 + 160*66560/12000.0;
    if(trPeriod==1800) txt=1.0 + 160*134400/12000.0;
  }
  if(mode=="MSK144" or bFast9) {
    txt=trPeriod-0.25; // JT9-fast, MSK144
  }
  if(mode=="Echo") txt=2.4;
  return txt;
}
