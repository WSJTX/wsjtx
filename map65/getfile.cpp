#include "getfile.h"
#include <QDir>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

extern qint16 id[4*60*96000];

void getfile(QString fname, bool xpol, int dbDgrd)
{
  int npts=2*56*96000;
  if(xpol) npts=2*npts;

// Degrade S/N by dbDgrd dB -- for tests only!!
  float dgrd=0.0;
  if(dbDgrd<0) dgrd = 23.0*sqrt(pow(10.0,-0.1*(double)dbDgrd) - 1.0);
  float fac=23.0/sqrt(dgrd*dgrd + 23.0*23.0);

  memset(id,0,2*npts);
  char name[80];
  strcpy(name,fname.toLocal8Bit());
  FILE* fp=fopen(name,"rb");

  if(fp != NULL) {
    auto n = fread(&datcom_.fcenter,sizeof(datcom_.fcenter),1,fp);
    n = fread(id,2,npts,fp);
    Q_UNUSED (n);
    int j=0;

    if(dbDgrd<0) {
      for(int i=0; i<npts; i+=2) {
        datcom_.d4[j++]=fac*((float)id[i] + dgrd*gran());
        datcom_.d4[j++]=fac*((float)id[i+1] + dgrd*gran());
        if(!xpol) j+=2;               //Skip over d4(3,x) and d4(4,x)
      }
    } else {
      for(int i=0; i<npts; i+=2) {
        datcom_.d4[j++]=(float)id[i];
        datcom_.d4[j++]=(float)id[i+1];
        if(!xpol) j+=2;               //Skip over d4(3,x) and d4(4,x)
      }
    }
    fclose(fp);

    datcom_.ndiskdat=1;
    int nfreq=(int)datcom_.fcenter;
    if(nfreq!=144 and nfreq != 432 and nfreq != 1296) datcom_.fcenter=144.125;
    int i0=fname.indexOf(".tf2");
    if(i0<0) i0=fname.indexOf(".iq");
    datcom_.nutc=0;
    if(i0>0) {
      datcom_.nutc=100*fname.mid(i0-4,2).toInt() + fname.mid(i0-2,2).toInt();
    }
  }
}

void savetf2(QString fname, bool xpol)
{
  int npts=2*56*96000;
  if(xpol) npts=2*npts;

  qint16* buf=(qint16*)malloc(2*npts);
  char name[80];
  strcpy(name,fname.toLocal8Bit());
  FILE* fp=fopen(name,"wb");

  if(fp != NULL) {
    fwrite(&datcom_.fcenter,sizeof(datcom_.fcenter),1,fp);
    int j=0;
    for(int i=0; i<npts; i+=2) {
      buf[i]=(qint16)datcom_.d4[j++];
      buf[i+1]=(qint16)datcom_.d4[j++];
      if(!xpol) j+=2;               //Skip over d4(3,x) and d4(4,x)
    }
    fwrite(buf,2,npts,fp);
    fclose(fp);
  }
  free(buf);
}

//#define	MAX_RANDOM	0x7fffffff

/* Generate gaussian random float with mean=0 and std_dev=1 */
float gran()
{
  float fac,rsq,v1,v2;
  static float gset;
  static int iset;

  if(iset){
    /* Already got one */
    iset = 0;
    return gset;
  }
  /* Generate two evenly distributed numbers between -1 and +1
   * that are inside the unit circle
   */
  do {
    v1 = 2.0 * (float)rand() / RAND_MAX - 1;
    v2 = 2.0 * (float)rand() / RAND_MAX - 1;
    rsq = v1*v1 + v2*v2;
  } while(rsq >= 1.0 || rsq == 0.0);
  fac = sqrt(-2.0*log(rsq)/rsq);
  gset = v1*fac;
  iset++;
  return v2*fac;
}
