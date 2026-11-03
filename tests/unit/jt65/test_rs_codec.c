/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rs2.h"

void rs_encode_(int *dgen, int *sent);
void rs_decode_(int *recd, int *eras_pos, int *no_eras, int *decoded, int *nerr);

static int ntests;

static void expect(int condition, const char *message)
{
  if(!condition) {
    fprintf(stderr,"RS codec failure: %s\n",message);
    exit(EXIT_FAILURE);
  }
}

static void make_codeword(void *rs, int data[12], int codeword[63], int frame)
{
  int i;

  for(i=0;i<12;i++) data[i]=(frame*17+i*7+3)&63;
  memcpy(codeword,data,12*sizeof(int));
  encode_rs_int(rs,data,codeword+12);
}

static void corrupt(int received[63], int positions[51], int errors, int frame)
{
  int i;

  for(i=0;i<errors;i++) {
    positions[i]=(i*17+frame*5)%63;
    received[positions[i]]^=1+(i*11)%63;
  }
}

static void test_vector(void *rs)
{
  static const int parity[51]={
    56,45,59,6,25,41,42,39,14,28,4,58,29,56,15,48,58,
    20,47,25,41,57,10,47,63,15,26,34,49,25,27,37,60,37,
    23,33,54,9,37,21,61,5,57,60,36,36,3,8,11,32,4
  };
  int data[12],codeword[63],received[63],positions[51];
  int dgen[12],sent[63],decoded[12],no_eras=0,nerr;
  int i;

  for(i=0;i<12;i++) data[i]=i;
  memcpy(codeword,data,sizeof(data));
  encode_rs_int(rs,data,codeword+12);
  expect(!memcmp(codeword+12,parity,sizeof(parity)),"known parity vector");
  memcpy(received,codeword,sizeof(received));
  expect(decode_rs_int(rs,received,positions,0,1)==0,"known codeword");
  expect(!memcmp(received,codeword,sizeof(received)),"known codeword unchanged");

  for(i=0;i<12;i++) dgen[i]=data[11-i];
  for(i=0;i<51;i++) sent[50-i]=parity[i];
  for(i=0;i<12;i++) sent[51+i]=dgen[i];
  memcpy(received,sent,sizeof(received));
  rs_decode_(received,positions,&no_eras,decoded,&nerr);
  expect(nerr==0 && !memcmp(decoded,dgen,sizeof(dgen)),"known wrapper codeword");
  rs_encode_(dgen,received);
  expect(!memcmp(received,sent,sizeof(sent)),"known wrapper parity");
  ntests++;
}

static void test_errors(void *rs)
{
  int data[12],codeword[63],received[63],positions[51];
  int sent[63],decoded[12],no_eras=0,nerr;
  int frame,errors;

  for(frame=0;frame<4;frame++) {
    make_codeword(rs,data,codeword,frame);
    rs_encode_(data,sent);
    for(errors=0;errors<=25;errors++) {
      memcpy(received,codeword,sizeof(received));
      corrupt(received,positions,errors,frame);
      nerr=decode_rs_int(rs,received,positions,0,1);
      expect(nerr==errors,"corrected symbol count");
      expect(!memcmp(received,codeword,sizeof(received)),"corrected codeword");

      memcpy(received,sent,sizeof(received));
      corrupt(received,positions,errors,frame);
      rs_decode_(received,positions,&no_eras,decoded,&nerr);
      expect(nerr==errors,"wrapper corrected symbol count");
      expect(!memcmp(decoded,data,sizeof(data)),"wrapper decoded data");
      ntests++;
    }
  }
}

static void test_erasures(void *rs)
{
  static const int cases[][2]={{30,10},{40,30},{51,51}};
  int data[12],codeword[63],received[63],positions[51];
  int frame,errors,erasures,nerr;

  for(frame=0;frame<3;frame++) {
    errors=cases[frame][0];
    erasures=cases[frame][1];
    make_codeword(rs,data,codeword,frame+4);
    memcpy(received,codeword,sizeof(received));
    corrupt(received,positions,errors,frame+4);
    nerr=decode_rs_int(rs,received,positions,erasures,1);
    expect(nerr==errors,"erasure corrected symbol count");
    expect(!memcmp(received,codeword,sizeof(received)),"erasure corrected codeword");
    ntests++;
  }
}

static void test_syndrome_reuse(void *rs)
{
  int data[12],codeword[63],corrupted[63],received[63],positions[51],erasures[51];
  int frame,nerr;

  for(frame=0;frame<4;frame++) {
    make_codeword(rs,data,codeword,frame+7);
    memcpy(corrupted,codeword,sizeof(corrupted));
    corrupt(corrupted,positions,30,frame+7);
    memcpy(erasures,positions,10*sizeof(int));
    memcpy(received,corrupted,sizeof(received));
    nerr=decode_rs_int(rs,received,positions,0,1);
    expect(nerr==-1,"initial hard-decision trial");

    memcpy(received,corrupted,sizeof(received));
    nerr=decode_rs_int(rs,received,erasures,10,0);
    expect(nerr==30,"syndrome reuse corrected symbol count");
    expect(!memcmp(received,codeword,sizeof(received)),"syndrome reuse codeword");
    ntests++;
  }
}

int main(void)
{
  void *rs=init_rs_int(6,0x43,3,1,51);

  expect(rs!=NULL,"codec initialization");
  test_vector(rs);
  test_errors(rs);
  test_erasures(rs);
  test_syndrome_reuse(rs);
  free_rs_int(rs);
  printf("RS codec tests passed: %d\n",ntests);
  return EXIT_SUCCESS;
}
