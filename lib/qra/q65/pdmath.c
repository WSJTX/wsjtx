// pdmath.c
// Elementary math on probability distributions
// 
// (c) 2016 - Nico Palermo, IV3NWV - Microtelecom Srl, Italy
// ------------------------------------------------------------------------------
// This file is part of the qracodes project, a Forward Error Control
// encoding/decoding package based on Q-ary RA (Repeat and Accumulate) LDPC codes.
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

#include "pdmath.h"

typedef const float *ppd_uniform;
typedef void  (*ppd_imul)(float*,const float*);
typedef float (*ppd_norm)(float*);

// define vector size in function of its logarithm in base 2
static const int pd_log2dim[7] = {
	1,2,4,8,16,32,64
};

// define uniform distributions of given size
static const float pd_uniform1[1] = {
	1.
};
static const float pd_uniform2[2] = {
	1./2., 1./2.
};
static const float pd_uniform4[4] = {
	1./4., 1./4.,1./4., 1./4.
};
static const float pd_uniform8[8] = {
	1./8., 1./8.,1./8., 1./8.,1./8., 1./8.,1./8., 1./8.
};
static const float pd_uniform16[16] = {
	1./16., 1./16., 1./16., 1./16.,1./16., 1./16.,1./16., 1./16.,
	1./16., 1./16., 1./16., 1./16.,1./16., 1./16.,1./16., 1./16.
};
static const float pd_uniform32[32] = {
	1./32., 1./32., 1./32., 1./32.,1./32., 1./32.,1./32., 1./32.,
	1./32., 1./32., 1./32., 1./32.,1./32., 1./32.,1./32., 1./32.,
	1./32., 1./32., 1./32., 1./32.,1./32., 1./32.,1./32., 1./32.,
	1./32., 1./32., 1./32., 1./32.,1./32., 1./32.,1./32., 1./32.
};
static const float pd_uniform64[64] = {
	1./64., 1./64., 1./64., 1./64.,1./64., 1./64.,1./64., 1./64.,
	1./64., 1./64., 1./64., 1./64.,1./64., 1./64.,1./64., 1./64.,
	1./64., 1./64., 1./64., 1./64.,1./64., 1./64.,1./64., 1./64.,
	1./64., 1./64., 1./64., 1./64.,1./64., 1./64.,1./64., 1./64.,
	1./64., 1./64., 1./64., 1./64.,1./64., 1./64.,1./64., 1./64.,
	1./64., 1./64., 1./64., 1./64.,1./64., 1./64.,1./64., 1./64.,
	1./64., 1./64., 1./64., 1./64.,1./64., 1./64.,1./64., 1./64.,
	1./64., 1./64., 1./64., 1./64.,1./64., 1./64.,1./64., 1./64.

};

static const ppd_uniform pd_uniform_tab[7] = {
	pd_uniform1,
	pd_uniform2,
	pd_uniform4,
	pd_uniform8,
	pd_uniform16,
	pd_uniform32,
	pd_uniform64
};

// returns a pointer to the uniform distribution of the given logsize
const float *pd_uniform(int nlogdim)
{
	return pd_uniform_tab[nlogdim];
}

// in-place multiplication functions
// compute dst = dst*src for any element of the distrib

static void pd_imul1(float *dst, const float *src)
{
		dst[0] *= src[0]; 
}

static void pd_imul2(float *dst, const float *src)
{
		dst[0] *= src[0]; dst[1] *= src[1]; 
}
static void pd_imul4(float *dst, const float *src)
{
		dst[0] *= src[0]; dst[1] *= src[1]; 
		dst[2] *= src[2]; dst[3] *= src[3]; 
}
static void pd_imul8(float *dst, const float *src)
{
		dst[0] *= src[0]; dst[1] *= src[1]; dst[2] *= src[2]; dst[3] *= src[3]; 
		dst[4] *= src[4]; dst[5] *= src[5]; dst[6] *= src[6]; dst[7] *= src[7]; 
}
static void pd_imul16(float *dst, const float *src)
{
		dst[0] *= src[0];  dst[1] *= src[1];  dst[2] *= src[2];  dst[3] *= src[3]; 
		dst[4] *= src[4];  dst[5] *= src[5];  dst[6] *= src[6];  dst[7] *= src[7]; 
		dst[8] *= src[8];  dst[9] *= src[9];  dst[10]*= src[10]; dst[11]*= src[11]; 
		dst[12]*= src[12]; dst[13]*= src[13]; dst[14]*= src[14]; dst[15]*= src[15]; 
}
static void pd_imul32(float *dst, const float *src)
{
	pd_imul16(dst,src);
	pd_imul16(dst+16,src+16);
}
static void pd_imul64(float *dst, const float *src)
{
	pd_imul16(dst, src);
	pd_imul16(dst+16, src+16);
	pd_imul16(dst+32, src+32);
	pd_imul16(dst+48, src+48);
}

static const ppd_imul pd_imul_tab[7] = {
	pd_imul1,
	pd_imul2,
	pd_imul4,
	pd_imul8,
	pd_imul16,
	pd_imul32,
	pd_imul64
};

// in place multiplication
// compute dst = dst*src for any element of the distrib give their log2 size
// arguments must be pointers to array of floats of the given size
void pd_imul(float *dst, const float *src, int nlogdim)
{
	pd_imul_tab[nlogdim](dst,src);
}

static float pd_norm1(float *ppd)
{
	float t = ppd[0];
	ppd[0] = 1.f;
	return t;
}

static float pd_norm2(float *ppd)
{
	float t,to;

	t =ppd[0]; t +=ppd[1];

	if (t<=0) {
		pd_init(ppd,pd_uniform(1),pd_log2dim[1]);
		return t;
		}

	to = t;
	t = 1.f/t;
	ppd[0] *=t; ppd[1] *=t; 
	return to;

}

static float pd_norm4(float *ppd)
{
	float t,to;

	t =ppd[0]; t +=ppd[1]; t +=ppd[2]; t +=ppd[3];

	if (t<=0) {
		pd_init(ppd,pd_uniform(2),pd_log2dim[2]);
		return t;
		}

	to = t;
	t = 1.f/t;
	ppd[0] *=t; ppd[1] *=t; ppd[2] *=t; ppd[3] *=t; 
	return to;
}

static float pd_norm8(float *ppd)
{
	float t,to;

	t  =ppd[0]; t +=ppd[1]; t +=ppd[2]; t +=ppd[3];
	t +=ppd[4]; t +=ppd[5]; t +=ppd[6]; t +=ppd[7];

	if (t<=0) {
		pd_init(ppd,pd_uniform(3),pd_log2dim[3]);
		return t;
		}

	to = t;
	t = 1.f/t;
	ppd[0] *=t; ppd[1] *=t; ppd[2] *=t; ppd[3] *=t; 
	ppd[4] *=t; ppd[5] *=t; ppd[6] *=t; ppd[7] *=t; 
	return to;
}
static float pd_norm16(float *ppd)
{
	float t,to;

	t  =ppd[0];  t +=ppd[1];  t +=ppd[2];  t +=ppd[3];
	t +=ppd[4];  t +=ppd[5];  t +=ppd[6];  t +=ppd[7];
	t +=ppd[8];  t +=ppd[9];  t +=ppd[10]; t +=ppd[11];
	t +=ppd[12]; t +=ppd[13]; t +=ppd[14]; t +=ppd[15];

	if (t<=0) {
		pd_init(ppd,pd_uniform(4),pd_log2dim[4]);
		return t;
		}

	to = t;
	t = 1.f/t;
	ppd[0]  *=t; ppd[1]  *=t; ppd[2]  *=t; ppd[3]  *=t; 
	ppd[4]  *=t; ppd[5]  *=t; ppd[6]  *=t; ppd[7]  *=t; 
	ppd[8]  *=t; ppd[9]  *=t; ppd[10] *=t; ppd[11] *=t; 
	ppd[12] *=t; ppd[13] *=t; ppd[14] *=t; ppd[15] *=t; 

	return to;
}
static float pd_norm32(float *ppd)
{
	float t,to;

	t  =ppd[0];  t +=ppd[1];  t +=ppd[2];  t +=ppd[3];
	t +=ppd[4];  t +=ppd[5];  t +=ppd[6];  t +=ppd[7];
	t +=ppd[8];  t +=ppd[9];  t +=ppd[10]; t +=ppd[11];
	t +=ppd[12]; t +=ppd[13]; t +=ppd[14]; t +=ppd[15];
	t +=ppd[16]; t +=ppd[17]; t +=ppd[18]; t +=ppd[19];
	t +=ppd[20]; t +=ppd[21]; t +=ppd[22]; t +=ppd[23];
	t +=ppd[24]; t +=ppd[25]; t +=ppd[26]; t +=ppd[27];
	t +=ppd[28]; t +=ppd[29]; t +=ppd[30]; t +=ppd[31];

	if (t<=0) {
		pd_init(ppd,pd_uniform(5),pd_log2dim[5]);
		return t;
		}

	to = t;
	t = 1.f/t;
	ppd[0]  *=t; ppd[1]  *=t; ppd[2]  *=t; ppd[3]  *=t; 
	ppd[4]  *=t; ppd[5]  *=t; ppd[6]  *=t; ppd[7]  *=t; 
	ppd[8]  *=t; ppd[9]  *=t; ppd[10] *=t; ppd[11] *=t; 
	ppd[12] *=t; ppd[13] *=t; ppd[14] *=t; ppd[15] *=t; 
	ppd[16] *=t; ppd[17] *=t; ppd[18] *=t; ppd[19] *=t; 
	ppd[20] *=t; ppd[21] *=t; ppd[22] *=t; ppd[23] *=t; 
	ppd[24] *=t; ppd[25] *=t; ppd[26] *=t; ppd[27] *=t; 
	ppd[28] *=t; ppd[29] *=t; ppd[30] *=t; ppd[31] *=t; 

	return to;
}

static float pd_norm64(float *ppd)
{
	float t,to;

	t  =ppd[0];  t +=ppd[1];  t +=ppd[2];  t +=ppd[3];
	t +=ppd[4];  t +=ppd[5];  t +=ppd[6];  t +=ppd[7];
	t +=ppd[8];  t +=ppd[9];  t +=ppd[10]; t +=ppd[11];
	t +=ppd[12]; t +=ppd[13]; t +=ppd[14]; t +=ppd[15];
	t +=ppd[16]; t +=ppd[17]; t +=ppd[18]; t +=ppd[19];
	t +=ppd[20]; t +=ppd[21]; t +=ppd[22]; t +=ppd[23];
	t +=ppd[24]; t +=ppd[25]; t +=ppd[26]; t +=ppd[27];
	t +=ppd[28]; t +=ppd[29]; t +=ppd[30]; t +=ppd[31];

	t +=ppd[32]; t +=ppd[33]; t +=ppd[34]; t +=ppd[35];
	t +=ppd[36]; t +=ppd[37]; t +=ppd[38]; t +=ppd[39];
	t +=ppd[40]; t +=ppd[41]; t +=ppd[42]; t +=ppd[43];
	t +=ppd[44]; t +=ppd[45]; t +=ppd[46]; t +=ppd[47];
	t +=ppd[48]; t +=ppd[49]; t +=ppd[50]; t +=ppd[51];
	t +=ppd[52]; t +=ppd[53]; t +=ppd[54]; t +=ppd[55];
	t +=ppd[56]; t +=ppd[57]; t +=ppd[58]; t +=ppd[59];
	t +=ppd[60]; t +=ppd[61]; t +=ppd[62]; t +=ppd[63];

	if (t<=0) {
		pd_init(ppd,pd_uniform(6),pd_log2dim[6]);
		return t;
		}

	to = t;
	t = 1.0f/t;
	ppd[0]  *=t; ppd[1]  *=t; ppd[2]  *=t; ppd[3]  *=t; 
	ppd[4]  *=t; ppd[5]  *=t; ppd[6]  *=t; ppd[7]  *=t; 
	ppd[8]  *=t; ppd[9]  *=t; ppd[10] *=t; ppd[11] *=t; 
	ppd[12] *=t; ppd[13] *=t; ppd[14] *=t; ppd[15] *=t; 
	ppd[16] *=t; ppd[17] *=t; ppd[18] *=t; ppd[19] *=t; 
	ppd[20] *=t; ppd[21] *=t; ppd[22] *=t; ppd[23] *=t; 
	ppd[24] *=t; ppd[25] *=t; ppd[26] *=t; ppd[27] *=t; 
	ppd[28] *=t; ppd[29] *=t; ppd[30] *=t; ppd[31] *=t; 

	ppd[32] *=t; ppd[33] *=t; ppd[34] *=t; ppd[35] *=t; 
	ppd[36] *=t; ppd[37] *=t; ppd[38] *=t; ppd[39] *=t; 
	ppd[40] *=t; ppd[41] *=t; ppd[42] *=t; ppd[43] *=t; 
	ppd[44] *=t; ppd[45] *=t; ppd[46] *=t; ppd[47] *=t; 
	ppd[48] *=t; ppd[49] *=t; ppd[50] *=t; ppd[51] *=t; 
	ppd[52] *=t; ppd[53] *=t; ppd[54] *=t; ppd[55] *=t; 
	ppd[56] *=t; ppd[57] *=t; ppd[58] *=t; ppd[59] *=t; 
	ppd[60] *=t; ppd[61] *=t; ppd[62] *=t; ppd[63] *=t; 

	return to;
}


static const ppd_norm pd_norm_tab[7] = {
	pd_norm1,
	pd_norm2,
	pd_norm4,
	pd_norm8,
	pd_norm16,
	pd_norm32,
	pd_norm64
};

float pd_norm(float *pd, int nlogdim)
{
	return pd_norm_tab[nlogdim](pd);
}

void pd_memset(float *dst, const float *src, int ndim, int nitems)
{
	int size = PD_SIZE(ndim);
	while(nitems--) {
		memcpy(dst,src,size);
		dst +=ndim;
		}
}

void  pd_fwdperm(float *dst, float *src, const  int *perm, int ndim)
{
	// TODO: non-loop implementation
	while (ndim--) 
		dst[ndim] = src[perm[ndim]];
}

void  pd_bwdperm(float *dst, float *src, const  int *perm, int ndim)
{
	// TODO: non-loop implementation
	while (ndim--) 
		dst[perm[ndim]] = src[ndim];
}

float pd_max(float *src, int ndim)
{
	// TODO: faster implementation

	float cmax=0;  // we assume that prob distributions are always positive
	float cval;

	while (ndim--) {
		cval = src[ndim];
		if (cval>=cmax) {
			cmax = cval;
			}
		}

	return cmax;
}

int pd_argmax(float *pmax, float *src, int ndim)
{
	// TODO: faster implementation

	float cmax=0;  // we assume that prob distributions are always positive
	float cval;
	int idxmax=-1; // indicates that all pd elements are <0

	while (ndim--) {
		cval = src[ndim];
		if (cval>=cmax) {
			cmax = cval;
			idxmax = ndim;
			}
		}

	if (pmax)
		*pmax = cmax;

	return idxmax;
}
