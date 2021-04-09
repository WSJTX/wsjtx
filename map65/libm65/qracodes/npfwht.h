// np_fwht.h
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

#ifndef _npfwht_h_
#define _npfwht_h_

#ifdef __cplusplus
extern "C" {
#endif

void np_fwht(int nlogdim, float *dst, float *src);
// Compute the Walsh-Hadamard transform of the given data up to a 
// 64-dimensional transform
//
// Input parameters:
//		nlogdim:  log2 of the transform size. Must be in the range [0..6]
//      src    :  pointer to the input  data buffer. 
//      dst    :  pointer to the output data buffer. 
//
// src and dst must point to preallocated data buffers of size 2^nlogdim*sizeof(float) 
// src and dst buffers can overlap

#ifdef __cplusplus
}
#endif

#endif // _npfwht_
