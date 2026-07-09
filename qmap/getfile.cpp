#include "getfile.h"
#include <QFile>
#include <algorithm>
#include <array>
#include <vector>
#include <stdlib.h>
#include <math.h>

void getfile(QString fname, int dbDgrd)
{
//  int npts=2*56*96000;
  int npts=2*60*96000;

// Degrade S/N by dbDgrd dB -- for tests only!!
  float dgrd=0.0;
  if(dbDgrd<0) dgrd = 23.0*sqrt(pow(10.0,-0.1*(double)dbDgrd) - 1.0);
  float fac=23.0/sqrt(dgrd*dgrd + 23.0*23.0);

  QFile file(fname);

  if(file.open(QIODevice::ReadOnly)) {
    file.read(reinterpret_cast<char *>(&datcom_.fcenter), sizeof(datcom_.fcenter));
    std::array<qint16, 16384> samples;
    int j=0;
    while(j<npts) {
      int want=std::min<int>(samples.size(),npts-j);
      qint64 bytes_read=file.read(reinterpret_cast<char *>(samples.data()),
                                  want * static_cast<qint64>(sizeof(qint16)));
      size_t n=bytes_read > 0
        ? static_cast<size_t>(bytes_read / static_cast<qint64>(sizeof(qint16)))
        : 0;
      for(size_t i=0; i<n; ++i) {
        datcom_.d4[j++]=dbDgrd<0
          ? fac*((float)samples[i] + dgrd*gran())
          : (float)samples[i];
      }
      if(n<static_cast<size_t>(want)) break;
    }
    while(j<npts) datcom_.d4[j++]=0.0;
    qint64 ntx30_size=static_cast<qint64>(sizeof(datcom_.ntx30a));
    bool read_ntx30a=file.read(reinterpret_cast<char *>(&datcom_.ntx30a),
                               ntx30_size) == ntx30_size;
    bool read_ntx30b=file.read(reinterpret_cast<char *>(&datcom_.ntx30b),
                               ntx30_size) == ntx30_size;
    if(!read_ntx30a || !read_ntx30b) {
      datcom_.ntx30a=0;
      datcom_.ntx30b=0;
    }

    datcom_.ndiskdat=1;
  //  int nfreq=(int)datcom_.fcenter;
  //  if(nfreq!=144 and nfreq != 432 and nfreq != 1296) datcom_.fcenter=1296.090;
    int i0=fname.indexOf(".iq");
    datcom_.nutc=0;
    if(i0>0) {
      datcom_.nutc=100*fname.mid(i0-4,2).toInt() + fname.mid(i0-2,2).toInt();
    }
  }
}

void save_iq(QString fname)
{
  int npts=2*60*96000;
  std::vector<qint16> buf(npts);
  QFile file(fname);

  if(file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    file.write(reinterpret_cast<char const *>(&datcom_.fcenter),
               sizeof(datcom_.fcenter));
    int j=0;
    for(int i=0; i<npts; i+=2) {
      buf[i]=(qint16)qRound(datcom_.d4[j++]);
      buf[i+1]=(qint16)qRound(datcom_.d4[j++]);
    }
    file.write(reinterpret_cast<char const *>(buf.data()),
               static_cast<qint64>(buf.size() * sizeof(qint16)));
    qint64 ntx30_size=static_cast<qint64>(sizeof(datcom_.ntx30a));
    file.write(reinterpret_cast<char const *>(&datcom_.ntx30a),
               2 * ntx30_size);
  }
}

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
