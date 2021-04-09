#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
// #include <sys/times.h>
// #include <time.h>
// #include <sys/time.h>
#include "sleep.h"
#include "timeval.h"

/*  FORTRAN:  fd = close(filedes)      */
int close_(int *filedes)
{
return(close(*filedes));
}
/*  FORTRAN:  fd = open(filnam,mode)  */
int open_(char filnam[], int *mode)
{
  return(open(filnam,*mode));
}
/* FORTRAN:  fd = creat(filnam,mode) */
int creat_(char filnam[],int *mode)
{
  return(creat(filnam,*mode));
}
/* FORTRAN:  nread = read(fd,buf,n) */
int read_(int *fd, char buf[], int *n)
{
  return(read(*fd,buf,*n));
}
/* FORTRAN:  nwrt = write(fd,buf,n) */
int write_(int *fd, char buf[], int *n)
{
  return(write(*fd,buf,*n));
}
/* FORTRAN: ns = lseek(fd,offset,origin) */
int lseek_(int *fd,int *offset, int *origin)
{
  return(lseek(*fd,*offset,*origin));
}
/* times(2) */
//int times_(struct tms *buf)
//{
//  return (times(buf));
//}
/* ioperm(2) */
//ioperm_(from,num,turn_on)
//unsigned long *from,*num,*turn_on;
//{
//  return (ioperm(*from,*num,*turn_on));
//   return (i386_get_ioperm(*from,*num,*turn_on));
//}

/* usleep(3) */
void usleep_(unsigned long *microsec)
{
  usleep(*microsec);
}

/* returns random numbers between 0 and 32767 to FORTRAN program */
int iran_(int *arg)
{
  return (rand());
}

int exit_(int *n)
{
  printf("\n\n");
  exit(*n);
}

/*
struct tm *
gmtime_r_(const time_t *clock, struct tm *result)
{
  gmtime_r(clock, result);
}
*/

time_t time_(void)
{
     return time(0);
}

/* hrtime() */
double hrtime_(void)
{
  struct timeval tv;
  gettimeofday(&tv,NULL);
  return(tv.tv_sec+1.e-6*tv.tv_usec);
}
