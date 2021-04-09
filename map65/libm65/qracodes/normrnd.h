// normrnd.h
// Functions to generate gaussian distributed numbers
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

#ifndef _normrnd_h_
#define _normrnd_h_

#define _CRT_RAND_S
#include <stdlib.h>

#define _USE_MATH_DEFINES 
#include <math.h>
#define M_2PI (2.0f*(float)M_PI)

#ifdef __cplusplus
extern "C" {
#endif

void normrnd_s(float *dst, int nitems, float mean, float stdev);
// generate a random array of numbers with a gaussian distribution of given mean and stdev
// use MS rand_s(...) function

/* not used
void normrnd(float *dst, int nitems, float mean, float stdev);
// generate a random array of numbers with a gaussian distribution of given mean and stdev
// use MS rand() function
*/

#ifdef __cplusplus
}
#endif

#endif // _normrnd_h_

