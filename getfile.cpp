#include "getfile.h"
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
#include <QRandomGenerator>
#include <random>
#endif

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifdef WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <err.h>
#endif

//#define	MAX_RANDOM	0x7fffffff
/* Generate gaussian random float with mean=0 and std_dev=1 */
float gran()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  static std::normal_distribution<float> d;
  return d (*QRandomGenerator::global ());
#else
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
    v1 = 2.0 * (float)qrand() / RAND_MAX - 1;
    v2 = 2.0 * (float)qrand() / RAND_MAX - 1;
    rsq = v1*v1 + v2*v2;
  } while(rsq >= 1.0 || rsq == 0.0);
  fac = sqrt(-2.0*log(rsq)/rsq);
  gset = v1*fac;
  iset++;
  return v2*fac;
#endif
}
