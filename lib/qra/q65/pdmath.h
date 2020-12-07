// pdmath.h
// Elementary math on probability distributions
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


#ifndef _pdmath_h_
#define _pdmath_h_

#include <memory.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PD_NDIM(nlogdim)              ((1<<(nlogdim))
#define PD_SIZE(ndim)                 ((ndim)*sizeof(float))
#define PD_ROWADDR(fp,ndim,idx)       (fp+((ndim)*(idx)))		

const float *pd_uniform(int nlogdim);
// Returns a pointer to a (constant) uniform distribution of the given log2 size

#define      pd_init(dst,src,ndim) memcpy(dst,src,PD_SIZE(ndim))
// Distribution copy

void         pd_memset(float *dst, const float *src, int ndim, int nitems);
// Copy the distribution pointed by src to the array of distributions dst
// src is a pointer to the input distribution (a vector of size ndim)
// dst is a pointer to a linear array of distributions (a vector of size ndim*nitems)

void         pd_imul(float *dst, const float *src, int nlogdim);
// In place multiplication
// Compute dst = dst*src for any element of the distrib give their log2 size
// src and dst arguments must be pointers to array of floats of the given size

float        pd_norm(float *pd, int nlogdim);
// In place normalizazion
// Normalizes the input vector so that the sum of its components are one
// pd must be a pointer to an array of floats of the given size.
// If the norm of the input vector is non-positive the vector components
// are replaced with a uniform distribution
// Returns the norm of the distribution prior to the normalization

void         pd_fwdperm(float *dst, float *src, const  int *perm, int ndim);
// Forward permutation of a distribution
// Computes dst[k] = src[perm[k]] for every element in the distribution
// perm must be a pointer to an array of integers of length ndim

void         pd_bwdperm(float *dst, float *src, const  int *perm, int ndim);
// Backward permutation of a distribution
// Computes dst[perm[k]] = src[k] for every element in the distribution
// perm must be a pointer to an array of integers of length ndim

float        pd_max(float *src, int ndim);
// Return the maximum of the elements of the given distribution
// Assumes that the input vector is a probability distribution and that each element in the
// distribution is non negative

int          pd_argmax(float *pmax, float *src, int ndim);
// Return the index of the maximum element of the given distribution
// The maximum is stored in the variable pointed by pmax if pmax is not null
// Same note of pd_max applies. 
// Return -1 if all the elements in the distribution are negative

#ifdef __cplusplus
}
#endif

#endif // _pdmath_h_
