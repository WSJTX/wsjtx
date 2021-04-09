// qra64_subs.c
// Fortran interface routines for QRA64

#include "qra64.h"
#include <stdio.h>

static qra64codec *pqra64codec = NULL;

void qra64_enc_(int x[], int y[])
{
  if (pqra64codec==NULL) pqra64codec = qra64_init(QRA_USERAP);  
  qra64_encode(pqra64codec, y, x);
}

void qra64_dec_(float r[], int* nc1, int* nc2, int* ng2, int* APtype, 
		int* iset, int* ns0, float* b0, int* nf0,
		int xdec[], float* snr, int* rc)
{
/*
  APtype:                         AP
-----------------------------------------------------------------------
     -1                            0     (no AP information)
      0    [CQ/QRZ    ?      ? ]  25/37
      1    [MyCall    ?      ? ]  25/37
      2    [  ?    HisCall   ? ]  25/37
      3    [MyCall HisCall   ? ]  49/68
      4    [MyCall HisCall grid]  68
      5    [CQ/QRZ HisCall   ? ]  49/68

     rc    Message format         AP APTYPE Comments
------------------------------------------------------------------------
    -16                                     Failed sanity check
     -2                                     Decoded but CRC failed
     -1                                     No decode
      0    [   ?      ?      ? ]   0   -1   Decode with no AP info
      1    [CQ/QRZ    ?      ? ]  25    0
      2    [CQ/QRZ    ?      _ ]  37    0
      3    [MyCall    ?      ? ]  25    1
      4    [MyCall    ?      _ ]  37    1
      5    [MyCall HisCall   ? ]  49    3
      6    [   ?   HisCall   ? ]  25    2   Optional
      7    [   ?   HisCall   _ ]  37    2   Optional
      8    [MyCall HisCall Grid]  68    4
      9    [CQ/QRZ HisCall   ? ]  49    5   Optional (not needed?)
     10    [CQ/QRZ HisCall   _ ]  68    5   Optional
     11    [CQ/QRZ HisCall Grid]  68    ?   Optional
*/

  float EbNodBEstimated;
  int err=0;
  int nSubmode=*ns0;
  float b90=*b0;
  int nFadingModel=*nf0;

  if(pqra64codec==NULL) pqra64codec = qra64_init(QRA_USERAP);
  err=qra64_apset(pqra64codec,*nc1,*nc2,*ng2,*APtype);
  if(err<0) printf("ERROR: qra64_apset returned %d\n",err);

  if(*iset==0) {
    *rc = qra64_decode_fastfading(pqra64codec,&EbNodBEstimated,xdec,r,
    				  nSubmode,b90,nFadingModel);
    *snr = EbNodBEstimated - 31.0;
  }
}

