#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define RADS 0.0174532925199433
#define DEGS 57.2957795130823
#define TPI 6.28318530717959
#define PI 3.1415927

/* ratio of     earth radius to astronomical unit */
#define ER_OVER_AU 0.0000426352325194252

/* all prototypes here */

double getcoord(int coord);
void getargs(int argc, char *argv[], int *y, int *m, double *tz, double *glong, double *glat);
double range(double y);
double rangerad(double y);
double days(int y, int m, int dn, double hour);
double days_(int *y, int *m, int *dn, double *hour);
void moonpos(double, double *, double *, double *);
void sunpos(double , double *, double *, double *);
double moontransit(int y, int m, int d, double timezone, double glat, double glong, int *nt);
double atan22(double y, double x);
double epsilon(double d);
void equatorial(double d, double *lon, double *lat, double *r);
void ecliptic(double d, double *lon, double *lat, double *r);
double gst(double d);
void topo(double lst, double glat, double *alp, double *dec, double *r);
double alt(double glat, double ha, double dec);
void libration(double day, double lambda, double beta, double alpha, double *l, double *b, double *p);
void illumination(double day, double lra, double ldec, double dr, double sra, double sdec, double *pabl, double *ill);
int daysinmonth(int y, int m);
int isleap(int y);
void tmoonsub_(double *day, double *glat, double *glong, double *moonalt, 
   double *mrv, double *l, double *b, double *paxis);

static const char
usage[] = "  Usage: tmoon date[yyyymm] timz[+/-h.hh] long[+/-dddmm] lat[+/-ddmm]\n"
            "example: tmoon 200009 0 -00155 5230\n";

/*
  getargs() gets the arguments from the command line, does some basic error
  checking, and converts arguments into numerical form. Arguments are passed
  back in pointers. Error messages print to stderr so re-direction of output
  to file won't leave users blind. Error checking prints list of all errors
  in a command line before quitting.
*/
void getargs(int argc, char *argv[], int *y, int *m, double *tz,
             double *glong, double *glat) {

  int date, latitude, longitude;
  int mflag = 0, yflag = 0, longflag = 0, latflag = 0, tzflag = 0;
  int longminflag = 0, latminflag = 0, dflag = 0;

  /* if not right number of arguments, then print example command line */

  if (argc !=5) {
    fprintf(stderr, usage);
    exit(EXIT_FAILURE);
  }

  date = atoi(argv[1]);
  *y = date / 100;
  *m = date - *y * 100;
  *tz = (double) atof(argv[2]);
  longitude = atoi(argv[3]);
  latitude = atoi(argv[4]);
  *glong = RADS * getcoord(longitude);
  *glat = RADS * getcoord(latitude);

  /* set a flag for each error found */

  if (*m > 12 || *m < 1) mflag = 1;
  if (*y > 2500) yflag = 1;
  if (date < 150001) dflag = 1;
  if (fabs((float) *glong) > 180 * RADS) longflag = 1;
  if (abs(longitude) % 100 > 59) longminflag = 1;
  if (fabs((float) *glat) > 90 * RADS) latflag = 1;
  if (abs(latitude) % 100 > 59) latminflag = 1;
  if (fabs((float) *tz) > 12) tzflag = 1;

  /* print all the errors found */
  
  if (dflag == 1) {
    fprintf(stderr, "date: dates must be in form yyyymm, gregorian, and later than 1500 AD\n");
  }
  if (yflag == 1) {
    fprintf(stderr, "date: too far in future - accurate from 1500 to 2500\n");
  }
  if (mflag == 1) {
    fprintf(stderr, "date: month must be in range 0 to 12, eg - August 2000 is entered as 200008\n");
  }
  if (tzflag == 1) {
    fprintf(stderr, "timz: must be in range +/- 12 hours, eg -6 for Chicago\n");
  }
  if (longflag == 1) {
    fprintf(stderr, "long: must be in range +/- 180 degrees\n");
  }
  if (longminflag == 1) {
    fprintf(stderr, "long: last two digits are arcmin - max 59\n");
  }
  if (latflag == 1) {
    fprintf(stderr, " lat: must be in range +/- 90 degrees\n");
  }
  if (latminflag == 1) {
    fprintf(stderr, " lat: last two digits are arcmin - max 59\n");
  }

  /* quits if one or more flags set */

  if (dflag + mflag + yflag + longflag + latflag + tzflag + longminflag + latminflag > 0) {
    exit(EXIT_FAILURE);
  }
  
}

/*
   returns coordinates in decimal degrees given the
   coord as a ddmm value stored in an integer.
*/
double getcoord(int coord) {
  int west = 1;
  double glg, deg;
  if (coord < 0) west = -1;
  glg = fabs((double) coord/100);
  deg = floor(glg);
  glg = west* (deg + (glg - deg)*100 / 60);
  return(glg);
}

/*
  days() takes the year, month, day in the month and decimal hours
  in the day and returns the number of days since J2000.0.
  Assumes Gregorian calendar.
*/
double days(int y, int m, int d, double h) {
  int a, b;
  double day;
  
  /*
    The lines below work from 1900 march to feb 2100
    a = 367 * y - 7 * (y + (m + 9) / 12) / 4 + 275 * m / 9 + d;
    day = (double)a - 730531.5 + hour / 24;
  */

  /*  These lines work for any Gregorian date since 0 AD */
  if (m ==1 || m==2) {
    m +=12;
    y -= 1;
  }
  a = y / 100;
  b = 2 - a + a/4;
  day = floor(365.25*(y + 4716)) + floor(30.6001*(m + 1))
    + d + b - 1524.5 - 2451545 + h/24;
  return(day);
}
double days_(int *y0, int *m0, int *d0, double *h0) 
{
  return days(*y0,*m0,*d0,*h0);
}

/*
Returns 1 if y a leap year, and 0 otherwise, according
to the Gregorian calendar
*/
int isleap(int y) {
  int a = 0;
  if(y % 4 == 0) a = 1;
  if(y % 100 == 0) a = 0;
  if(y % 400 == 0) a = 1;
  return(a);
}

/*
Given the year and the month, function returns the
number of days in the month. Valid for Gregorian
calendar.
*/
int daysinmonth(int y, int m) {
  int b = 31;
  if(m == 2) {
    if(isleap(y) == 1) b= 29;
    else b = 28;
  }
  if(m == 4 || m == 6 || m == 9 || m == 11) b = 30;
  return(b);
}

/*
moonpos() takes days from J2000.0 and returns ecliptic coordinates
of moon in the pointers. Note call by reference.
This function is within a couple of arcminutes most of the time,
and is truncated from the Meeus Ch45 series, themselves truncations of
ELP-2000. Returns moon distance in earth radii.
Terms have been written out explicitly rather than using the
table based method as only a small number of terms is
retained.
*/
void moonpos(double d, double *lambda, double *beta, double *rvec) {
  double dl, dB, dR, L, D, M, M1, F, e, lm, bm, rm, t;

  t = d / 36525;

  L = range(218.3164591  + 481267.88134236  * t) * RADS;
  D = range(297.8502042  + 445267.1115168  * t) * RADS;
  M = range(357.5291092  + 35999.0502909  * t) * RADS;
  M1 = range(134.9634114  + 477198.8676313  * t - .008997 * t * t) * RADS;
  F = range(93.27209929999999  + 483202.0175273  * t - .0034029*t*t)*RADS;
  e = 1 - .002516 * t;

  dl =      6288774 * sin(M1);
  dl +=     1274027 * sin(2 * D - M1);
  dl +=      658314 * sin(2 * D);
  dl +=      213618 * sin(2 * M1);
  dl -=  e * 185116 * sin(M);
  dl -=      114332 * sin(2 * F) ;
  dl +=       58793 * sin(2 * D - 2 * M1);
  dl +=   e * 57066 * sin(2 * D - M - M1) ;
  dl +=       53322 * sin(2 * D + M1);
  dl +=   e * 45758 * sin(2 * D - M);
  dl -=   e * 40923 * sin(M - M1);
  dl -=       34720 * sin(D) ;
  dl -=   e * 30383 * sin(M + M1) ;
  dl +=       15327 * sin(2 * D - 2 * F) ;
  dl -=       12528 * sin(M1 + 2 * F);
  dl +=       10980 * sin(M1 - 2 * F);
  lm = rangerad(L + dl / 1000000 * RADS);

  dB =   5128122 * sin(F);
  dB +=   280602 * sin(M1 + F);
  dB +=   277693 * sin(M1 - F);
  dB +=   173237 * sin(2 * D - F);
  dB +=    55413 * sin(2 * D - M1 + F);
  dB +=    46271 * sin(2 * D - M1 - F);
  dB +=    32573 * sin(2 * D + F);
  dB +=    17198 * sin(2 * M1 + F);
  dB +=     9266 * sin(2 * D + M1 - F);
  dB +=     8822 * sin(2 * M1 - F);
  dB += e * 8216 * sin(2 * D - M - F);
  dB +=     4324 * sin(2 * D - 2 * M1 - F);
  bm = dB / 1000000 * RADS;

  dR =    -20905355 * cos(M1);
  dR -=     3699111 * cos(2 * D - M1);
  dR -=     2955968 * cos(2 * D);
  dR -=      569925 * cos(2 * M1);
  dR +=   e * 48888 * cos(M);
  dR -=        3149 * cos(2 * F);
  dR +=      246158 * cos(2 * D - 2 * M1);
  dR -=  e * 152138 * cos(2 * D - M - M1) ;
  dR -=      170733 * cos(2 * D + M1);
  dR -=  e * 204586 * cos(2 * D - M);
  dR -=  e * 129620 * cos(M - M1);
  dR +=      108743 * cos(D);
  dR +=  e * 104755 * cos(M + M1);
  dR +=       79661 * cos(M1 - 2 * F);
  rm = 385000.56  + dR / 1000;

  *lambda = lm;
  *beta = bm;
  /* distance to Moon must be in Earth radii */
  *rvec = rm / 6378.14;
}

/*
topomoon() takes the local siderial time, the geographical
latitude of the observer, and pointers to the geocentric
equatorial coordinates. The function overwrites the geocentric
coordinates with topocentric coordinates on a simple spherical
earth model (no polar flattening). Expects Moon-Earth distance in
Earth radii.    Formulas scavenged from Astronomical Almanac 'low
precision formulae for Moon position' page D46.
*/

void topo(double lst, double glat, double *alp, double *dec, double *r) {
  double x, y, z, r1;
  x = *r * cos(*dec) * cos(*alp) - cos(glat) * cos(lst);
  y = *r * cos(*dec) * sin(*alp) - cos(glat) * sin(lst);
  z = *r * sin(*dec)  - sin(glat);
  r1 = sqrt(x*x + y*y + z*z);
  *alp = atan22(y, x);
  *dec = asin(z / r1);
  *r = r1;
}

/*
moontransit() takes date, the time zone and geographic longitude
of observer and returns the time (decimal hours) of lunar transit
on that day if there is one, and sets the notransit flag if there
isn't. See Explanatory Supplement to Astronomical Almanac
section 9.32 and 9.31 for the method.
*/

double moontransit(int y, int m, int d, double tz, double glat, double glong, int *notransit) {
  double hm, ht, ht1, lon, lat, rv, dnew, lst;
  int itcount;

  ht1 = 180 * RADS;
  ht = 0;
  itcount = 0;
  *notransit = 0;
  do {
    ht = ht1;
    itcount++;
    dnew = days(y, m, d, ht * DEGS/15) - tz/24;
    lst = gst(dnew) + glong;
    /* find the topocentric Moon ra (hence hour angle) and dec */
    moonpos(dnew, &lon, &lat, &rv);
    equatorial(dnew, &lon, &lat, &rv);
    topo(lst, glat, &lon, &lat, &rv);
    hm = rangerad(lst -  lon);
    ht1 = rangerad(ht - hm);
    /* if no convergence, then no transit on that day */
    if (itcount > 30) {
      *notransit = 1;
      break;
    }
  }
  while (fabs(ht - ht1) > 0.04 * RADS);
  return(ht1);
}

/*
  Calculates the selenographic coordinates of either the sub Earth point
  (optical libration) or the sub-solar point (selen. coords of centre of
  bright hemisphere).  Based on Meeus chapter 51 but neglects physical
  libration and nutation, with some simplification of the formulas.
*/
void libration(double day, double lambda, double beta, double alpha, double *l, double *b, double *p) {
  double i, f, omega, w, y, x, a, t, eps;
  t = day / 36525;
  i = 1.54242 * RADS;
  eps = epsilon(day);
  f = range(93.2720993 + 483202.0175273 * t - .0034029 * t * t) * RADS;
  omega = range(125.044555 - 1934.1361849 * t + .0020762 * t * t) * RADS;
  w = lambda - omega;
  y = sin(w) * cos(beta) * cos(i) - sin(beta) * sin(i);
  x = cos(w) * cos(beta);
  a = atan22(y, x);
  *l = a - f;

  /*  kludge to catch cases of 'round the back' angles  */
  if (*l < -90 * RADS) *l += TPI;
  if (*l > 90 * RADS)  *l -= TPI;
  *b = asin(-sin(w) * cos(beta) * sin(i) - sin(beta) * cos(i));

  /*  pa pole axis - not used for Sun stuff */
  x = sin(i) * sin(omega);
  y = sin(i) * cos(omega) * cos(eps) - cos(i) * sin(eps);
  w = atan22(x, y);
  *p = rangerad(asin(sqrt(x*x + y*y) * cos(alpha - w) / cos(*b)));
}

/*
  Takes: days since J2000.0, eq coords Moon, ratio of moon to sun distance,
  eq coords Sun
  Returns: position angle of bright limb wrt NCP, percentage illumination
  of Sun
*/
void illumination(double day , double lra, double ldec, double dr, double sra, double sdec, double *pabl, double *ill) {
  double x, y, phi, i;
  (void)day;
  y = cos(sdec) * sin(sra - lra);
  x = sin(sdec) * cos(ldec) - cos(sdec) * sin(ldec) * cos (sra - lra);
  *pabl = atan22(y, x);
  phi = acos(sin(sdec) * sin(ldec) + cos(sdec) * cos(ldec) * cos(sra-lra));
  i = atan22(sin(phi) , (dr - cos(phi)));
  *ill = 0.5*(1 + cos(i));
}

/*
sunpos() takes days from J2000.0 and returns ecliptic longitude
of Sun in the pointers. Latitude is zero at this level of precision,
but pointer left in for consistency in number of arguments.
This function is within 0.01 degree (1 arcmin) almost all the time
for a century either side of J2000.0. This is from the 'low precision
fomulas for the Sun' from C24 of Astronomical Alamanac
*/
void sunpos(double d, double *lambda, double *beta, double *rvec) {
  double L, g, ls, bs, rs;

  L = range(280.461 + .9856474 * d) * RADS;
  g = range(357.528 + .9856003 * d) * RADS;
  ls = L + (1.915 * sin(g) + .02 * sin(2 * g)) * RADS;
  bs = 0;
  rs = 1.00014 - .01671 * cos(g) - .00014 * cos(2 * g);
  *lambda = ls;
  *beta = bs;
  *rvec = rs;
}

/*
this routine returns the altitude given the days since J2000.0
the hour angle and declination of the object and the latitude
of the observer. Used to find the Sun's altitude to put a letter
code on the transit time, and to find the Moon's altitude at
transit just to make sure that the Moon is visible.
*/
double alt(double glat, double ha, double dec) {
  return(asin(sin(dec) * sin(glat) + cos(dec) * cos(glat) * cos(ha)));
}

/* returns an angle in degrees in the range 0 to 360 */
double range(double x) {
  double a, b;
  b = x / 360;
  a = 360 * (b - floor(b));
  if (a < 0)
    a = 360 + a;
  return(a);
}

/* returns an angle in rads in the range 0 to two pi */
double rangerad(double x) {
  double a, b;
  b = x / TPI;
  a = TPI * (b - floor(b));
  if (a < 0)
    a = TPI + a;
  return(a);
}

/*
gets the atan2 function returning angles in the right
order and  range
*/
double atan22(double y, double x) {
  double a;

  a = atan2(y, x);
  if (a < 0) a += TPI;
  return(a);
}

/*
returns mean obliquity of ecliptic in radians given days since
J2000.0.
*/
double epsilon(double d) {
  double t = d/ 36525;
  return((23.4392911111111 - (t* (46.8150 + 0.00059*t)/3600)) *RADS);
}

/*
replaces ecliptic coordinates with equatorial coordinates
note: call by reference destroys original values
R is unchanged.
*/
void equatorial(double d, double *lon, double *lat, double * r) {
  double  eps, ceps, seps, l, b;
  (void)r;

  l = *lon;
  b = * lat;
  eps = epsilon(d);
  ceps = cos(eps);
  seps = sin(eps);
  *lon = atan22(sin(l)*ceps - tan(b)*seps, cos(l));
  *lat = asin(sin(b)*ceps + cos(b)*seps*sin(l));
}

/*
replaces equatorial coordinates with ecliptic ones. Inverse
of above, but used to find topocentric ecliptic coords.
*/
void ecliptic(double d, double *lon, double *lat, double * r) {
  double  eps, ceps, seps, alp, dec;
  (void)r;

  alp = *lon;
  dec = *lat;
  eps = epsilon(d);
  ceps = cos(eps);
  seps = sin(eps);
  *lon = atan22(sin(alp)*ceps + tan(dec)*seps, cos(alp));
  *lat = asin(sin(dec)*ceps - cos(dec)*seps*sin(alp));
}

/*
returns the siderial time at greenwich meridian as
an angle in radians given the days since J2000.0
*/
double gst( double d) {
  double t = d / 36525;
  double theta;
  theta = range(280.46061837 + 360.98564736629 * d + 0.000387933 * t * t);
  return(theta * RADS);
}

void tmoonsub_(double *day, double *glat, double *glong, double *moonalt, 
   double *mrv, double *l, double *b, double *paxis)
{
  double mlambda, mbeta;
  double malpha, mdelta;
  double lst, mhr;
  double tlambda, tbeta, trv;

  lst = gst(*day) + *glong;
      
  /* find Moon topocentric coordinates for libration calculations */

  moonpos(*day, &mlambda, &mbeta, mrv);
  malpha = mlambda;
  mdelta = mbeta;
  equatorial(*day, &malpha, &mdelta, mrv);
  topo(lst, *glat, &malpha, &mdelta, mrv);
  mhr = rangerad(lst - malpha);
  *moonalt = alt(*glat, mhr, mdelta);
      
  /* Optical libration and Position angle of the Pole */

  tlambda = malpha;
  tbeta = mdelta;
  trv = *mrv;
  ecliptic(*day, &tlambda, &tbeta, &trv);
  libration(*day, tlambda, tbeta, malpha,  l, b, paxis);
}
