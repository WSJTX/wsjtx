// npfwht.c
// Basic implementation of the Fast Walsh-Hadamard Transforms
// 
// (c) 2016 - Nico Palermo, IV3NWV - Microtelecom Srl, Italy
// ------------------------------------------------------------------------------
// This file is part of the qracodes project, a Forward Error Control
// encoding/decoding package based on Q-ary RA (repeat and accumulate) LDPC codes.
//
//    qracodes is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//    qracodes is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.

//    You should have received a copy of the GNU General Public License
//    along with qracodes source distribution.  
//    If not, see <http://www.gnu.org/licenses/>.

#include "npfwht.h"

#define WHBFY(dst,src,base,offs,dist) { dst[base+offs]=src[base+offs]+src[base+offs+dist]; dst[base+offs+dist]=src[base+offs]-src[base+offs+dist]; }

typedef void (*pnp_fwht)(float*,float*);

static void np_fwht2(float *dst, float *src);

static void np_fwht1(float *dst, float *src);
static void np_fwht2(float *dst, float *src);
static void np_fwht4(float *dst, float *src);
static void np_fwht8(float *dst, float *src);
static void np_fwht16(float *dst, float *src);
static void np_fwht32(float *dst, float *src);
static void np_fwht64(float *dst, float *src);

static pnp_fwht np_fwht_tab[7] = {
	np_fwht1,
	np_fwht2,
	np_fwht4,
	np_fwht8,
	np_fwht16,
	np_fwht32,
	np_fwht64
};

void np_fwht(int nlogdim, float *dst, float *src)
{
	np_fwht_tab[nlogdim](dst,src);
}

static void np_fwht1(float *dst, float *src)
{	
	dst[0] = src[0];
}


static void np_fwht2(float *dst, float *src)
{
	float t[2];

	WHBFY(t,src,0,0,1);
	dst[0]= t[0];
	dst[1]= t[1];
}

static void np_fwht4(float *dst, float *src)
{
	float t[4];

	// group 1
	WHBFY(t,src,0,0,2); WHBFY(t,src,0,1,2); 
	// group 2
	WHBFY(dst,t,0,0,1); WHBFY(dst,t,2,0,1);
};


static void np_fwht8(float *dst, float *src)
{
	float t[16];
	float *t1=t, *t2=t+8;

	// group 1
	WHBFY(t1,src,0,0,4); WHBFY(t1,src,0,1,4); WHBFY(t1,src,0,2,4); WHBFY(t1,src,0,3,4);
	// group 2
	WHBFY(t2,t1,0,0,2);  WHBFY(t2,t1,0,1,2);  WHBFY(t2,t1,4,0,2);	WHBFY(t2,t1,4,1,2); 
	// group 3
	WHBFY(dst,t2,0,0,1); WHBFY(dst,t2,2,0,1); WHBFY(dst,t2,4,0,1); WHBFY(dst,t2,6,0,1);
};


static void np_fwht16(float *dst, float *src)
{
	float t[32];
	float *t1=t, *t2=t+16;

	// group 1
	WHBFY(t1,src,0,0,8); WHBFY(t1,src,0,1,8);  WHBFY(t1,src,0,2,8);  WHBFY(t1,src,0,3,8);
	WHBFY(t1,src,0,4,8); WHBFY(t1,src,0,5,8);  WHBFY(t1,src,0,6,8);  WHBFY(t1,src,0,7,8);
	// group 2
	WHBFY(t2,t1,0,0,4);  WHBFY(t2,t1,0,1,4);   WHBFY(t2,t1,0,2,4);	 WHBFY(t2,t1,0,3,4);
	WHBFY(t2,t1,8,0,4);  WHBFY(t2,t1,8,1,4);   WHBFY(t2,t1,8,2,4);   WHBFY(t2,t1,8,3,4); 
	// group 3
	WHBFY(t1,t2,0,0,2);  WHBFY(t1,t2,0,1,2);   WHBFY(t1,t2,4,0,2);   WHBFY(t1,t2,4,1,2); 
	WHBFY(t1,t2,8,0,2);  WHBFY(t1,t2,8,1,2);   WHBFY(t1,t2,12,0,2);  WHBFY(t1,t2,12,1,2); 
	// group 4
	WHBFY(dst,t1,0,0,1); WHBFY(dst,t1,2,0,1);  WHBFY(dst,t1,4,0,1);  WHBFY(dst,t1,6,0,1); 
	WHBFY(dst,t1,8,0,1); WHBFY(dst,t1,10,0,1); WHBFY(dst,t1,12,0,1); WHBFY(dst,t1,14,0,1); 

}

static void np_fwht32(float *dst, float *src)
{
	float t[64];
	float *t1=t, *t2=t+32;

	// group 1
	WHBFY(t1,src,0,0,16);  WHBFY(t1,src,0,1,16);  WHBFY(t1,src,0,2,16);  WHBFY(t1,src,0,3,16); 
	WHBFY(t1,src,0,4,16);  WHBFY(t1,src,0,5,16);  WHBFY(t1,src,0,6,16);  WHBFY(t1,src,0,7,16); 
	WHBFY(t1,src,0,8,16);  WHBFY(t1,src,0,9,16);  WHBFY(t1,src,0,10,16); WHBFY(t1,src,0,11,16); 
	WHBFY(t1,src,0,12,16); WHBFY(t1,src,0,13,16); WHBFY(t1,src,0,14,16); WHBFY(t1,src,0,15,16); 

	// group 2
	WHBFY(t2,t1,0,0,8);  WHBFY(t2,t1,0,1,8);  WHBFY(t2,t1,0,2,8);  WHBFY(t2,t1,0,3,8);
	WHBFY(t2,t1,0,4,8);  WHBFY(t2,t1,0,5,8);  WHBFY(t2,t1,0,6,8);  WHBFY(t2,t1,0,7,8);
	WHBFY(t2,t1,16,0,8); WHBFY(t2,t1,16,1,8); WHBFY(t2,t1,16,2,8); WHBFY(t2,t1,16,3,8); 
	WHBFY(t2,t1,16,4,8); WHBFY(t2,t1,16,5,8); WHBFY(t2,t1,16,6,8); WHBFY(t2,t1,16,7,8); 

	// group 3
	WHBFY(t1,t2,0,0,4);  WHBFY(t1,t2,0,1,4);  WHBFY(t1,t2,0,2,4);  WHBFY(t1,t2,0,3,4);
	WHBFY(t1,t2,8,0,4);  WHBFY(t1,t2,8,1,4);  WHBFY(t1,t2,8,2,4);  WHBFY(t1,t2,8,3,4);
	WHBFY(t1,t2,16,0,4); WHBFY(t1,t2,16,1,4); WHBFY(t1,t2,16,2,4); WHBFY(t1,t2,16,3,4);
	WHBFY(t1,t2,24,0,4); WHBFY(t1,t2,24,1,4); WHBFY(t1,t2,24,2,4); WHBFY(t1,t2,24,3,4);

	// group 4
	WHBFY(t2,t1,0,0,2);  WHBFY(t2,t1,0,1,2);  WHBFY(t2,t1,4,0,2);  WHBFY(t2,t1,4,1,2); 
	WHBFY(t2,t1,8,0,2);  WHBFY(t2,t1,8,1,2);  WHBFY(t2,t1,12,0,2); WHBFY(t2,t1,12,1,2); 
	WHBFY(t2,t1,16,0,2); WHBFY(t2,t1,16,1,2); WHBFY(t2,t1,20,0,2); WHBFY(t2,t1,20,1,2); 
	WHBFY(t2,t1,24,0,2); WHBFY(t2,t1,24,1,2); WHBFY(t2,t1,28,0,2); WHBFY(t2,t1,28,1,2); 

	// group 5
	WHBFY(dst,t2,0,0,1);   WHBFY(dst,t2,2,0,1);  WHBFY(dst,t2,4,0,1);   WHBFY(dst,t2,6,0,1); 
	WHBFY(dst,t2,8,0,1);   WHBFY(dst,t2,10,0,1); WHBFY(dst,t2,12,0,1);  WHBFY(dst,t2,14,0,1); 
	WHBFY(dst,t2,16,0,1);  WHBFY(dst,t2,18,0,1); WHBFY(dst,t2,20,0,1);  WHBFY(dst,t2,22,0,1); 
	WHBFY(dst,t2,24,0,1);  WHBFY(dst,t2,26,0,1); WHBFY(dst,t2,28,0,1);  WHBFY(dst,t2,30,0,1); 

}

static void np_fwht64(float *dst, float *src)
{
	float t[128];
	float *t1=t, *t2=t+64;


	// group 1
	WHBFY(t1,src,0,0,32);  WHBFY(t1,src,0,1,32);  WHBFY(t1,src,0,2,32);  WHBFY(t1,src,0,3,32); 
	WHBFY(t1,src,0,4,32);  WHBFY(t1,src,0,5,32);  WHBFY(t1,src,0,6,32);  WHBFY(t1,src,0,7,32); 
	WHBFY(t1,src,0,8,32);  WHBFY(t1,src,0,9,32);  WHBFY(t1,src,0,10,32); WHBFY(t1,src,0,11,32); 
	WHBFY(t1,src,0,12,32); WHBFY(t1,src,0,13,32); WHBFY(t1,src,0,14,32); WHBFY(t1,src,0,15,32); 
	WHBFY(t1,src,0,16,32); WHBFY(t1,src,0,17,32); WHBFY(t1,src,0,18,32); WHBFY(t1,src,0,19,32); 
	WHBFY(t1,src,0,20,32); WHBFY(t1,src,0,21,32); WHBFY(t1,src,0,22,32); WHBFY(t1,src,0,23,32); 
	WHBFY(t1,src,0,24,32); WHBFY(t1,src,0,25,32); WHBFY(t1,src,0,26,32); WHBFY(t1,src,0,27,32); 
	WHBFY(t1,src,0,28,32); WHBFY(t1,src,0,29,32); WHBFY(t1,src,0,30,32); WHBFY(t1,src,0,31,32); 

	// group 2
	WHBFY(t2,t1,0,0,16);   WHBFY(t2,t1,0,1,16);  WHBFY(t2,t1,0,2,16);   WHBFY(t2,t1,0,3,16);
	WHBFY(t2,t1,0,4,16);   WHBFY(t2,t1,0,5,16);  WHBFY(t2,t1,0,6,16);   WHBFY(t2,t1,0,7,16);
	WHBFY(t2,t1,0,8,16);   WHBFY(t2,t1,0,9,16);  WHBFY(t2,t1,0,10,16);  WHBFY(t2,t1,0,11,16);
	WHBFY(t2,t1,0,12,16);  WHBFY(t2,t1,0,13,16); WHBFY(t2,t1,0,14,16);  WHBFY(t2,t1,0,15,16);

	WHBFY(t2,t1,32,0,16);  WHBFY(t2,t1,32,1,16);  WHBFY(t2,t1,32,2,16);  WHBFY(t2,t1,32,3,16);
	WHBFY(t2,t1,32,4,16);  WHBFY(t2,t1,32,5,16);  WHBFY(t2,t1,32,6,16);  WHBFY(t2,t1,32,7,16);
	WHBFY(t2,t1,32,8,16);  WHBFY(t2,t1,32,9,16);  WHBFY(t2,t1,32,10,16); WHBFY(t2,t1,32,11,16);
	WHBFY(t2,t1,32,12,16); WHBFY(t2,t1,32,13,16); WHBFY(t2,t1,32,14,16); WHBFY(t2,t1,32,15,16);

	// group 3
	WHBFY(t1,t2,0,0,8);    WHBFY(t1,t2,0,1,8); 	 WHBFY(t1,t2,0,2,8);    WHBFY(t1,t2,0,3,8);
	WHBFY(t1,t2,0,4,8);    WHBFY(t1,t2,0,5,8); 	 WHBFY(t1,t2,0,6,8);    WHBFY(t1,t2,0,7,8);
	WHBFY(t1,t2,16,0,8);   WHBFY(t1,t2,16,1,8);  WHBFY(t1,t2,16,2,8);   WHBFY(t1,t2,16,3,8);
	WHBFY(t1,t2,16,4,8);   WHBFY(t1,t2,16,5,8);  WHBFY(t1,t2,16,6,8);   WHBFY(t1,t2,16,7,8);
	WHBFY(t1,t2,32,0,8);   WHBFY(t1,t2,32,1,8);  WHBFY(t1,t2,32,2,8);   WHBFY(t1,t2,32,3,8);
	WHBFY(t1,t2,32,4,8);   WHBFY(t1,t2,32,5,8);  WHBFY(t1,t2,32,6,8);   WHBFY(t1,t2,32,7,8);
	WHBFY(t1,t2,48,0,8);   WHBFY(t1,t2,48,1,8);  WHBFY(t1,t2,48,2,8);   WHBFY(t1,t2,48,3,8);
	WHBFY(t1,t2,48,4,8);   WHBFY(t1,t2,48,5,8);  WHBFY(t1,t2,48,6,8);   WHBFY(t1,t2,48,7,8);

	// group 4
	WHBFY(t2,t1,0,0,4);   WHBFY(t2,t1,0,1,4); 	WHBFY(t2,t1,0,2,4);  WHBFY(t2,t1,0,3,4); 
	WHBFY(t2,t1,8,0,4);   WHBFY(t2,t1,8,1,4); 	WHBFY(t2,t1,8,2,4);  WHBFY(t2,t1,8,3,4); 
	WHBFY(t2,t1,16,0,4);  WHBFY(t2,t1,16,1,4); 	WHBFY(t2,t1,16,2,4); WHBFY(t2,t1,16,3,4); 
	WHBFY(t2,t1,24,0,4);  WHBFY(t2,t1,24,1,4); 	WHBFY(t2,t1,24,2,4); WHBFY(t2,t1,24,3,4); 
	WHBFY(t2,t1,32,0,4);  WHBFY(t2,t1,32,1,4); 	WHBFY(t2,t1,32,2,4); WHBFY(t2,t1,32,3,4); 
	WHBFY(t2,t1,40,0,4);  WHBFY(t2,t1,40,1,4); 	WHBFY(t2,t1,40,2,4); WHBFY(t2,t1,40,3,4); 
	WHBFY(t2,t1,48,0,4);  WHBFY(t2,t1,48,1,4); 	WHBFY(t2,t1,48,2,4); WHBFY(t2,t1,48,3,4); 
	WHBFY(t2,t1,56,0,4);  WHBFY(t2,t1,56,1,4); 	WHBFY(t2,t1,56,2,4); WHBFY(t2,t1,56,3,4); 

	// group 5
	WHBFY(t1,t2,0,0,2);  WHBFY(t1,t2,0,1,2);  WHBFY(t1,t2,4,0,2);  WHBFY(t1,t2,4,1,2);
	WHBFY(t1,t2,8,0,2);  WHBFY(t1,t2,8,1,2);  WHBFY(t1,t2,12,0,2); WHBFY(t1,t2,12,1,2);
	WHBFY(t1,t2,16,0,2); WHBFY(t1,t2,16,1,2); WHBFY(t1,t2,20,0,2); WHBFY(t1,t2,20,1,2);
	WHBFY(t1,t2,24,0,2); WHBFY(t1,t2,24,1,2); WHBFY(t1,t2,28,0,2); WHBFY(t1,t2,28,1,2);
	WHBFY(t1,t2,32,0,2); WHBFY(t1,t2,32,1,2); WHBFY(t1,t2,36,0,2); WHBFY(t1,t2,36,1,2);
	WHBFY(t1,t2,40,0,2); WHBFY(t1,t2,40,1,2); WHBFY(t1,t2,44,0,2); WHBFY(t1,t2,44,1,2);
	WHBFY(t1,t2,48,0,2); WHBFY(t1,t2,48,1,2); WHBFY(t1,t2,52,0,2); WHBFY(t1,t2,52,1,2);
	WHBFY(t1,t2,56,0,2); WHBFY(t1,t2,56,1,2); WHBFY(t1,t2,60,0,2); WHBFY(t1,t2,60,1,2);

	// group 6
	WHBFY(dst,t1,0,0,1);   WHBFY(dst,t1,2,0,1);  WHBFY(dst,t1,4,0,1);  WHBFY(dst,t1,6,0,1); 
	WHBFY(dst,t1,8,0,1);   WHBFY(dst,t1,10,0,1); WHBFY(dst,t1,12,0,1); WHBFY(dst,t1,14,0,1); 
	WHBFY(dst,t1,16,0,1);  WHBFY(dst,t1,18,0,1); WHBFY(dst,t1,20,0,1); WHBFY(dst,t1,22,0,1); 
	WHBFY(dst,t1,24,0,1);  WHBFY(dst,t1,26,0,1); WHBFY(dst,t1,28,0,1); WHBFY(dst,t1,30,0,1); 
	WHBFY(dst,t1,32,0,1);  WHBFY(dst,t1,34,0,1); WHBFY(dst,t1,36,0,1); WHBFY(dst,t1,38,0,1); 
	WHBFY(dst,t1,40,0,1);  WHBFY(dst,t1,42,0,1); WHBFY(dst,t1,44,0,1); WHBFY(dst,t1,46,0,1); 
	WHBFY(dst,t1,48,0,1);  WHBFY(dst,t1,50,0,1); WHBFY(dst,t1,52,0,1); WHBFY(dst,t1,54,0,1); 
	WHBFY(dst,t1,56,0,1);  WHBFY(dst,t1,58,0,1); WHBFY(dst,t1,60,0,1); WHBFY(dst,t1,62,0,1); 
}