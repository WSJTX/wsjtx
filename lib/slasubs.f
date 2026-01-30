      SUBROUTINE sla_CLDJ (IY, IM, ID, DJM, J)
*+
*     - - - - -
*      C L D J
*     - - - - -
*
*  Gregorian Calendar to Modified Julian Date
*
*  Given:
*     IY,IM,ID     int    year, month, day in Gregorian calendar
*
*  Returned:
*     DJM          dp     modified Julian Date (JD-2400000.5) for 0 hrs
*     J            int    status:
*                           0 = OK
*                           1 = bad year   (MJD not computed)
*                           2 = bad month  (MJD not computed)
*                           3 = bad day    (MJD computed)
*
*  The year must be -4699 (i.e. 4700BC) or later.
*
*  The algorithm is adapted from Hatcher 1984 (QJRAS 25, 53-55).
*
*  Last revision:   27 July 2004
*
*  Copyright P.T.Wallace.  All rights reserved.
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      INTEGER IY,IM,ID
      DOUBLE PRECISION DJM
      INTEGER J

*  Month lengths in days
      INTEGER MTAB(12)
      DATA MTAB / 31,28,31,30,31,30,31,31,30,31,30,31 /



*  Preset status.
      J = 0

*  Validate year.
      IF ( IY .LT. -4699 ) THEN
         J = 1
      ELSE

*     Validate month.
         IF ( IM.GE.1 .AND. IM.LE.12 ) THEN

*        Allow for leap year.
            IF ( MOD(IY,4) .EQ. 0 ) THEN
               MTAB(2) = 29
            ELSE
               MTAB(2) = 28
            END IF
            IF ( MOD(IY,100).EQ.0 .AND. MOD(IY,400).NE.0 )
     :         MTAB(2) = 28

*        Validate day.
            IF ( ID.LT.1 .OR. ID.GT.MTAB(IM) ) J=3

*        Modified Julian Date.
            DJM = DBLE ( ( 1461 * ( IY - (12-IM)/10 + 4712 ) ) / 4
     :               + ( 306 * MOD ( IM+9, 12 ) + 5 ) / 10
     :               - ( 3 * ( ( IY - (12-IM)/10 + 4900 ) / 100 ) ) / 4
     :               + ID - 2399904 )

*        Bad month.
         ELSE
            J=2
         END IF

      END IF

      END
      DOUBLE PRECISION FUNCTION sla_DAT (UTC)
*+
*     - - - -
*      D A T
*     - - - -
*
*  Increment to be applied to Coordinated Universal Time UTC to give
*  International Atomic Time TAI (double precision)
*
*  Given:
*     UTC      d      UTC date as a modified JD (JD-2400000.5)
*
*  Result:  TAI-UTC in seconds
*
*  Notes:
*
*  1  The UTC is specified to be a date rather than a time to indicate
*     that care needs to be taken not to specify an instant which lies
*     within a leap second.  Though in most cases UTC can include the
*     fractional part, correct behaviour on the day of a leap second
*     can only be guaranteed up to the end of the second 23:59:59.
*
*  2  For epochs from 1961 January 1 onwards, the expressions from the
*     file ftp://maia.usno.navy.mil/ser7/tai-utc.dat are used.
*
*  3  The 5ms time step at 1961 January 1 is taken from 2.58.1 (p87) of
*     the 1992 Explanatory Supplement.
*
*  4  UTC began at 1960 January 1.0 (JD 2436934.5) and it is improper
*     to call the routine with an earlier epoch.  However, if this
*     is attempted, the TAI-UTC expression for the year 1960 is used.
*
*
*     :-----------------------------------------:
*     :                                         :
*     :                IMPORTANT                :
*     :                                         :
*     :  This routine must be updated on each   :
*     :     occasion that a leap second is      :
*     :                announced                :
*     :                                         :
*     :  Latest leap second:  2016 Dec 31       :
*     :                                         :
*     :-----------------------------------------:
*
*  Last revision:   30 January 2026
*
*  Copyright P.T.Wallace.  All rights reserved.
*-

      IMPLICIT NONE

      DOUBLE PRECISION UTC

      DOUBLE PRECISION DT



      IF (.FALSE.) THEN

* - - - - - - - - - - - - - - - - - - - - - - *
*  Add new code here on each occasion that a  *
*  leap second is announced, and update the   *
*  preamble comments appropriately.           *
* - - - - - - - - - - - - - - - - - - - - - - *

*     2017 January 1
      ELSE IF (UTC.GE.57754D0) THEN
         DT=37D0

*     2015 July 1
      ELSE IF (UTC.GE.57204D0) THEN
         DT=36D0

*     2012 July 1
      ELSE IF (UTC.GE.56109D0) THEN
         DT=35D0

*     2009 January 1
      ELSE IF (UTC.GE.54832D0) THEN
         DT=34D0

*     2006 January 1
      ELSE IF (UTC.GE.53736D0) THEN
         DT=33D0

*     1999 January 1
      ELSE IF (UTC.GE.51179D0) THEN
         DT=32D0

*     1997 July 1
      ELSE IF (UTC.GE.50630D0) THEN
         DT=31D0

*     1996 January 1
      ELSE IF (UTC.GE.50083D0) THEN
         DT=30D0

*     1994 July 1
      ELSE IF (UTC.GE.49534D0) THEN
         DT=29D0

*     1993 July 1
      ELSE IF (UTC.GE.49169D0) THEN
         DT=28D0

*     1992 July 1
      ELSE IF (UTC.GE.48804D0) THEN
         DT=27D0

*     1991 January 1
      ELSE IF (UTC.GE.48257D0) THEN
         DT=26D0

*     1990 January 1
      ELSE IF (UTC.GE.47892D0) THEN
         DT=25D0

*     1988 January 1
      ELSE IF (UTC.GE.47161D0) THEN
         DT=24D0

*     1985 July 1
      ELSE IF (UTC.GE.46247D0) THEN
         DT=23D0

*     1983 July 1
      ELSE IF (UTC.GE.45516D0) THEN
         DT=22D0

*     1982 July 1
      ELSE IF (UTC.GE.45151D0) THEN
         DT=21D0

*     1981 July 1
      ELSE IF (UTC.GE.44786D0) THEN
         DT=20D0

*     1980 January 1
      ELSE IF (UTC.GE.44239D0) THEN
         DT=19D0

*     1979 January 1
      ELSE IF (UTC.GE.43874D0) THEN
         DT=18D0

*     1978 January 1
      ELSE IF (UTC.GE.43509D0) THEN
         DT=17D0

*     1977 January 1
      ELSE IF (UTC.GE.43144D0) THEN
         DT=16D0

*     1976 January 1
      ELSE IF (UTC.GE.42778D0) THEN
         DT=15D0

*     1975 January 1
      ELSE IF (UTC.GE.42413D0) THEN
         DT=14D0

*     1974 January 1
      ELSE IF (UTC.GE.42048D0) THEN
         DT=13D0

*     1973 January 1
      ELSE IF (UTC.GE.41683D0) THEN
         DT=12D0

*     1972 July 1
      ELSE IF (UTC.GE.41499D0) THEN
         DT=11D0

*     1972 January 1
      ELSE IF (UTC.GE.41317D0) THEN
         DT=10D0

*     1968 February 1
      ELSE IF (UTC.GE.39887D0) THEN
         DT=4.2131700D0+(UTC-39126D0)*0.002592D0

*     1966 January 1
      ELSE IF (UTC.GE.39126D0) THEN
         DT=4.3131700D0+(UTC-39126D0)*0.002592D0

*     1965 September 1
      ELSE IF (UTC.GE.39004D0) THEN
         DT=3.8401300D0+(UTC-38761D0)*0.001296D0

*     1965 July 1
      ELSE IF (UTC.GE.38942D0) THEN
         DT=3.7401300D0+(UTC-38761D0)*0.001296D0

*     1965 March 1
      ELSE IF (UTC.GE.38820D0) THEN
         DT=3.6401300D0+(UTC-38761D0)*0.001296D0

*     1965 January 1
      ELSE IF (UTC.GE.38761D0) THEN
         DT=3.5401300D0+(UTC-38761D0)*0.001296D0

*     1964 September 1
      ELSE IF (UTC.GE.38639D0) THEN
         DT=3.4401300D0+(UTC-38761D0)*0.001296D0

*     1964 April 1
      ELSE IF (UTC.GE.38486D0) THEN
         DT=3.3401300D0+(UTC-38761D0)*0.001296D0

*     1964 January 1
      ELSE IF (UTC.GE.38395D0) THEN
         DT=3.2401300D0+(UTC-38761D0)*0.001296D0

*     1963 November 1
      ELSE IF (UTC.GE.38334D0) THEN
         DT=1.9458580D0+(UTC-37665D0)*0.0011232D0

*     1962 January 1
      ELSE IF (UTC.GE.37665D0) THEN
         DT=1.8458580D0+(UTC-37665D0)*0.0011232D0

*     1961 August 1
      ELSE IF (UTC.GE.37512D0) THEN
         DT=1.3728180D0+(UTC-37300D0)*0.001296D0

*     1961 January 1
      ELSE IF (UTC.GE.37300D0) THEN
         DT=1.4228180D0+(UTC-37300D0)*0.001296D0

*     Before that
      ELSE
         DT=1.4178180D0+(UTC-37300D0)*0.001296D0

      END IF

      sla_DAT=DT

      END
      SUBROUTINE sla_DC62S (V, A, B, R, AD, BD, RD)
*+
*     - - - - - -
*      D C 6 2 S
*     - - - - - -
*
*  Conversion of position & velocity in Cartesian coordinates
*  to spherical coordinates (double precision)
*
*  Given:
*     V      d(6)   Cartesian position & velocity vector
*
*  Returned:
*     A      d      longitude (radians)
*     B      d      latitude (radians)
*     R      d      radial coordinate
*     AD     d      longitude derivative (radians per unit time)
*     BD     d      latitude derivative (radians per unit time)
*     RD     d      radial derivative
*
*  P.T.Wallace   Starlink   28 April 1996
*
*  Copyright (C) 1996 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION V(6),A,B,R,AD,BD,RD

      DOUBLE PRECISION X,Y,Z,XD,YD,ZD,RXY2,RXY,R2,XYP



*  Components of position/velocity vector
      X=V(1)
      Y=V(2)
      Z=V(3)
      XD=V(4)
      YD=V(5)
      ZD=V(6)

*  Component of R in XY plane squared
      RXY2=X*X+Y*Y

*  Modulus squared
      R2=RXY2+Z*Z

*  Protection against null vector
      IF (R2.EQ.0D0) THEN
         X=XD
         Y=YD
         Z=ZD
         RXY2=X*X+Y*Y
         R2=RXY2+Z*Z
      END IF

*  Position and velocity in spherical coordinates
      RXY=SQRT(RXY2)
      XYP=X*XD+Y*YD
      IF (RXY2.NE.0D0) THEN
         A=ATAN2(Y,X)
         B=ATAN2(Z,RXY)
         AD=(X*YD-Y*XD)/RXY2
         BD=(ZD*RXY2-Z*XYP)/(R2*RXY)
      ELSE
         A=0D0
         IF (Z.NE.0D0) THEN
            B=ATAN2(Z,RXY)
         ELSE
            B=0D0
         END IF
         AD=0D0
         BD=0D0
      END IF
      R=SQRT(R2)
      IF (R.NE.0D0) THEN
         RD=(XYP+Z*ZD)/R
      ELSE
         RD=0D0
      END IF

      END
      SUBROUTINE sla_DCC2S (V, A, B)
*+
*     - - - - - -
*      D C C 2 S
*     - - - - - -
*
*  Cartesian to spherical coordinates (double precision)
*
*  Given:
*     V     d(3)   x,y,z vector
*
*  Returned:
*     A,B   d      spherical coordinates in radians
*
*  The spherical coordinates are longitude (+ve anticlockwise looking
*  from the +ve latitude pole) and latitude.  The Cartesian coordinates
*  are right handed, with the x axis at zero longitude and latitude, and
*  the z axis at the +ve latitude pole.
*
*  If V is null, zero A and B are returned.  At either pole, zero A is
*  returned.
*
*  Last revision:   22 July 2004
*
*  Copyright P.T.Wallace.  All rights reserved.
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION V(3),A,B

      DOUBLE PRECISION X,Y,Z,R


      X = V(1)
      Y = V(2)
      Z = V(3)
      R = SQRT(X*X+Y*Y)

      IF (R.EQ.0D0) THEN
         A = 0D0
      ELSE
         A = ATAN2(Y,X)
      END IF

      IF (Z.EQ.0D0) THEN
         B = 0D0
      ELSE
         B = ATAN2(Z,R)
      END IF

      END
      SUBROUTINE sla_DCS2C (A, B, V)
*+
*     - - - - - -
*      D C S 2 C
*     - - - - - -
*
*  Spherical coordinates to direction cosines (double precision)
*
*  Given:
*     A,B       d      spherical coordinates in radians
*                         (RA,Dec), (long,lat) etc.
*
*  Returned:
*     V         d(3)   x,y,z unit vector
*
*  The spherical coordinates are longitude (+ve anticlockwise looking
*  from the +ve latitude pole) and latitude.  The Cartesian coordinates
*  are right handed, with the x axis at zero longitude and latitude, and
*  the z axis at the +ve latitude pole.
*
*  Last revision:   26 December 2004
*
*  Copyright P.T.Wallace.  All rights reserved.
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION A,B,V(3)

      DOUBLE PRECISION COSB


      COSB = COS(B)

      V(1) = COS(A)*COSB
      V(2) = SIN(A)*COSB
      V(3) = SIN(B)

      END
      SUBROUTINE sla_DE2H (HA, DEC, PHI, AZ, EL)
*+
*     - - - - -
*      D E 2 H
*     - - - - -
*
*  Equatorial to horizon coordinates:  HA,Dec to Az,El
*
*  (double precision)
*
*  Given:
*     HA      d     hour angle
*     DEC     d     declination
*     PHI     d     observatory latitude
*
*  Returned:
*     AZ      d     azimuth
*     EL      d     elevation
*
*  Notes:
*
*  1)  All the arguments are angles in radians.
*
*  2)  Azimuth is returned in the range 0-2pi;  north is zero,
*      and east is +pi/2.  Elevation is returned in the range
*      +/-pi/2.
*
*  3)  The latitude must be geodetic.  In critical applications,
*      corrections for polar motion should be applied.
*
*  4)  In some applications it will be important to specify the
*      correct type of hour angle and declination in order to
*      produce the required type of azimuth and elevation.  In
*      particular, it may be important to distinguish between
*      elevation as affected by refraction, which would
*      require the "observed" HA,Dec, and the elevation
*      in vacuo, which would require the "topocentric" HA,Dec.
*      If the effects of diurnal aberration can be neglected, the
*      "apparent" HA,Dec may be used instead of the topocentric
*      HA,Dec.
*
*  5)  No range checking of arguments is carried out.
*
*  6)  In applications which involve many such calculations, rather
*      than calling the present routine it will be more efficient to
*      use inline code, having previously computed fixed terms such
*      as sine and cosine of latitude, and (for tracking a star)
*      sine and cosine of declination.
*
*  P.T.Wallace   Starlink   9 July 1994
*
*  Copyright (C) 1995 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION HA,DEC,PHI,AZ,EL

      DOUBLE PRECISION D2PI
      PARAMETER (D2PI=6.283185307179586476925286766559D0)

      DOUBLE PRECISION SH,CH,SD,CD,SP,CP,X,Y,Z,R,A


*  Useful trig functions
      SH=SIN(HA)
      CH=COS(HA)
      SD=SIN(DEC)
      CD=COS(DEC)
      SP=SIN(PHI)
      CP=COS(PHI)

*  Az,El as x,y,z
      X=-CH*CD*SP+SD*CP
      Y=-SH*CD
      Z=CH*CD*CP+SD*SP

*  To spherical
      R=SQRT(X*X+Y*Y)
      IF (R.EQ.0D0) THEN
         A=0D0
      ELSE
         A=ATAN2(Y,X)
      END IF
      IF (A.LT.0D0) A=A+D2PI
      AZ=A
      EL=ATAN2(Z,R)

      END
      SUBROUTINE sla_DEULER (ORDER, PHI, THETA, PSI, RMAT)
*+
*     - - - - - - -
*      D E U L E R
*     - - - - - - -
*
*  Form a rotation matrix from the Euler angles - three successive
*  rotations about specified Cartesian axes (double precision)
*
*  Given:
*    ORDER   c*(*)   specifies about which axes the rotations occur
*    PHI     d       1st rotation (radians)
*    THETA   d       2nd rotation (   "   )
*    PSI     d       3rd rotation (   "   )
*
*  Returned:
*    RMAT    d(3,3)  rotation matrix
*
*  A rotation is positive when the reference frame rotates
*  anticlockwise as seen looking towards the origin from the
*  positive region of the specified axis.
*
*  The characters of ORDER define which axes the three successive
*  rotations are about.  A typical value is 'ZXZ', indicating that
*  RMAT is to become the direction cosine matrix corresponding to
*  rotations of the reference frame through PHI radians about the
*  old Z-axis, followed by THETA radians about the resulting X-axis,
*  then PSI radians about the resulting Z-axis.
*
*  The axis names can be any of the following, in any order or
*  combination:  X, Y, Z, uppercase or lowercase, 1, 2, 3.  Normal
*  axis labelling/numbering conventions apply;  the xyz (=123)
*  triad is right-handed.  Thus, the 'ZXZ' example given above
*  could be written 'zxz' or '313' (or even 'ZxZ' or '3xZ').  ORDER
*  is terminated by length or by the first unrecognized character.
*
*  Fewer than three rotations are acceptable, in which case the later
*  angle arguments are ignored.  If all rotations are zero, the
*  identity matrix is produced.
*
*  P.T.Wallace   Starlink   23 May 1997
*
*  Copyright (C) 1997 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      CHARACTER*(*) ORDER
      DOUBLE PRECISION PHI,THETA,PSI,RMAT(3,3)

      INTEGER J,I,L,N,K
      DOUBLE PRECISION RESULT(3,3),ROTN(3,3),ANGLE,S,C,W,WM(3,3)
      CHARACTER AXIS



*  Initialize result matrix
      DO J=1,3
         DO I=1,3
            IF (I.NE.J) THEN
               RESULT(I,J) = 0D0
            ELSE
               RESULT(I,J) = 1D0
            END IF
         END DO
      END DO

*  Establish length of axis string
      L = LEN(ORDER)

*  Look at each character of axis string until finished
      DO N=1,3
         IF (N.LE.L) THEN

*        Initialize rotation matrix for the current rotation
            DO J=1,3
               DO I=1,3
                  IF (I.NE.J) THEN
                     ROTN(I,J) = 0D0
                  ELSE
                     ROTN(I,J) = 1D0
                  END IF
               END DO
            END DO

*        Pick up the appropriate Euler angle and take sine & cosine
            IF (N.EQ.1) THEN
               ANGLE = PHI
            ELSE IF (N.EQ.2) THEN
               ANGLE = THETA
            ELSE
               ANGLE = PSI
            END IF
            S = SIN(ANGLE)
            C = COS(ANGLE)

*        Identify the axis
            AXIS = ORDER(N:N)
            IF (AXIS.EQ.'X'.OR.
     :          AXIS.EQ.'x'.OR.
     :          AXIS.EQ.'1') THEN

*           Matrix for x-rotation
               ROTN(2,2) = C
               ROTN(2,3) = S
               ROTN(3,2) = -S
               ROTN(3,3) = C

            ELSE IF (AXIS.EQ.'Y'.OR.
     :               AXIS.EQ.'y'.OR.
     :               AXIS.EQ.'2') THEN

*           Matrix for y-rotation
               ROTN(1,1) = C
               ROTN(1,3) = -S
               ROTN(3,1) = S
               ROTN(3,3) = C

            ELSE IF (AXIS.EQ.'Z'.OR.
     :               AXIS.EQ.'z'.OR.
     :               AXIS.EQ.'3') THEN

*           Matrix for z-rotation
               ROTN(1,1) = C
               ROTN(1,2) = S
               ROTN(2,1) = -S
               ROTN(2,2) = C

            ELSE

*           Unrecognized character - fake end of string
               L = 0

            END IF

*        Apply the current rotation (matrix ROTN x matrix RESULT)
            DO I=1,3
               DO J=1,3
                  W = 0D0
                  DO K=1,3
                     W = W+ROTN(I,K)*RESULT(K,J)
                  END DO
                  WM(I,J) = W
               END DO
            END DO
            DO J=1,3
               DO I=1,3
                  RESULT(I,J) = WM(I,J)
               END DO
            END DO

         END IF

      END DO

*  Copy the result
      DO J=1,3
         DO I=1,3
            RMAT(I,J) = RESULT(I,J)
         END DO
      END DO

      END
      SUBROUTINE sla_DMOON (DATE, PV)
*+
*     - - - - - -
*      D M O O N
*     - - - - - -
*
*  Approximate geocentric position and velocity of the Moon
*  (double precision)
*
*  Given:
*     DATE       D       TDB (loosely ET) as a Modified Julian Date
*                                                    (JD-2400000.5)
*
*  Returned:
*     PV         D(6)    Moon x,y,z,xdot,ydot,zdot, mean equator and
*                                         equinox of date (AU, AU/s)
*
*  Notes:
*
*  1  This routine is a full implementation of the algorithm
*     published by Meeus (see reference).
*
*  2  Meeus quotes accuracies of 10 arcsec in longitude, 3 arcsec in
*     latitude and 0.2 arcsec in HP (equivalent to about 20 km in
*     distance).  Comparison with JPL DE200 over the interval
*     1960-2025 gives RMS errors of 3.7 arcsec and 83 mas/hour in
*     longitude, 2.3 arcsec and 48 mas/hour in latitude, 11 km
*     and 81 mm/s in distance.  The maximum errors over the same
*     interval are 18 arcsec and 0.50 arcsec/hour in longitude,
*     11 arcsec and 0.24 arcsec/hour in latitude, 40 km and 0.29 m/s
*     in distance.
*
*  3  The original algorithm is expressed in terms of the obsolete
*     timescale Ephemeris Time.  Either TDB or TT can be used, but
*     not UT without incurring significant errors (30 arcsec at
*     the present time) due to the Moon's 0.5 arcsec/sec movement.
*
*  4  The algorithm is based on pre IAU 1976 standards.  However,
*     the result has been moved onto the new (FK5) equinox, an
*     adjustment which is in any case much smaller than the
*     intrinsic accuracy of the procedure.
*
*  5  Velocity is obtained by a complete analytical differentiation
*     of the Meeus model.
*
*  Reference:
*     Meeus, l'Astronomie, June 1984, p348.
*
*  P.T.Wallace   Starlink   22 January 1998
*
*  Copyright (C) 1998 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION DATE,PV(6)

*  Degrees, arcseconds and seconds of time to radians
      DOUBLE PRECISION D2R,DAS2R,DS2R
      PARAMETER (D2R=0.0174532925199432957692369D0,
     :           DAS2R=4.848136811095359935899141D-6,
     :           DS2R=7.272205216643039903848712D-5)

*  Seconds per Julian century (86400*36525)
      DOUBLE PRECISION CJ
      PARAMETER (CJ=3155760000D0)

*  Julian epoch of B1950
      DOUBLE PRECISION B1950
      PARAMETER (B1950=1949.9997904423D0)

*  Earth equatorial radius in AU ( = 6378.137 / 149597870 )
      DOUBLE PRECISION ERADAU
      PARAMETER (ERADAU=4.2635212653763D-5)

      DOUBLE PRECISION T,THETA,SINOM,COSOM,DOMCOM,WA,DWA,WB,DWB,WOM,
     :                 DWOM,SINWOM,COSWOM,V,DV,COEFF,EMN,EMPN,DN,FN,EN,
     :                 DEN,DTHETA,FTHETA,EL,DEL,B,DB,BF,DBF,P,DP,SP,R,
     :                 DR,X,Y,Z,XD,YD,ZD,SEL,CEL,SB,CB,RCB,RBD,W,EPJ,
     :                 EQCOR,EPS,SINEPS,COSEPS,ES,EC
      INTEGER N,I

*
*  Coefficients for fundamental arguments
*
*   at J1900:  T**0, T**1, T**2, T**3
*   at epoch:  T**0, T**1
*
*  Units are degrees for position and Julian centuries for time
*

*  Moon's mean longitude
      DOUBLE PRECISION ELP0,ELP1,ELP2,ELP3,ELP,DELP
      PARAMETER (ELP0=270.434164D0,
     :           ELP1=481267.8831D0,
     :           ELP2=-0.001133D0,
     :           ELP3=0.0000019D0)

*  Sun's mean anomaly
      DOUBLE PRECISION EM0,EM1,EM2,EM3,EM,DEM
      PARAMETER (EM0=358.475833D0,
     :           EM1=35999.0498D0,
     :           EM2=-0.000150D0,
     :           EM3=-0.0000033D0)

*  Moon's mean anomaly
      DOUBLE PRECISION EMP0,EMP1,EMP2,EMP3,EMP,DEMP
      PARAMETER (EMP0=296.104608D0,
     :           EMP1=477198.8491D0,
     :           EMP2=0.009192D0,
     :           EMP3=0.0000144D0)

*  Moon's mean elongation
      DOUBLE PRECISION D0,D1,D2,D3,D,DD
      PARAMETER (D0=350.737486D0,
     :           D1=445267.1142D0,
     :           D2=-0.001436D0,
     :           D3=0.0000019D0)

*  Mean distance of the Moon from its ascending node
      DOUBLE PRECISION F0,F1,F2,F3,F,DF
      PARAMETER (F0=11.250889D0,
     :           F1=483202.0251D0,
     :           F2=-0.003211D0,
     :           F3=-0.0000003D0)

*  Longitude of the Moon's ascending node
      DOUBLE PRECISION OM0,OM1,OM2,OM3,OM,DOM
      PARAMETER (OM0=259.183275D0,
     :           OM1=-1934.1420D0,
     :           OM2=0.002078D0,
     :           OM3=0.0000022D0)

*  Coefficients for (dimensionless) E factor
      DOUBLE PRECISION E1,E2,E,DE,ESQ,DESQ
      PARAMETER (E1=-0.002495D0,E2=-0.00000752D0)

*  Coefficients for periodic variations etc
      DOUBLE PRECISION PAC,PA0,PA1
      PARAMETER (PAC=0.000233D0,PA0=51.2D0,PA1=20.2D0)
      DOUBLE PRECISION PBC
      PARAMETER (PBC=-0.001778D0)
      DOUBLE PRECISION PCC
      PARAMETER (PCC=0.000817D0)
      DOUBLE PRECISION PDC
      PARAMETER (PDC=0.002011D0)
      DOUBLE PRECISION PEC,PE0,PE1,PE2
      PARAMETER (PEC=0.003964D0,
     :                     PE0=346.560D0,PE1=132.870D0,PE2=-0.0091731D0)
      DOUBLE PRECISION PFC
      PARAMETER (PFC=0.001964D0)
      DOUBLE PRECISION PGC
      PARAMETER (PGC=0.002541D0)
      DOUBLE PRECISION PHC
      PARAMETER (PHC=0.001964D0)
      DOUBLE PRECISION PIC
      PARAMETER (PIC=-0.024691D0)
      DOUBLE PRECISION PJC,PJ0,PJ1
      PARAMETER (PJC=-0.004328D0,PJ0=275.05D0,PJ1=-2.30D0)
      DOUBLE PRECISION CW1
      PARAMETER (CW1=0.0004664D0)
      DOUBLE PRECISION CW2
      PARAMETER (CW2=0.0000754D0)

*
*  Coefficients for Moon position
*
*   Tx(N)       = coefficient of L, B or P term (deg)
*   ITx(N,1-5)  = coefficients of M, M', D, F, E**n in argument
*
      INTEGER NL,NB,NP
      PARAMETER (NL=50,NB=45,NP=31)
      DOUBLE PRECISION TL(NL),TB(NB),TP(NP)
      INTEGER ITL(5,NL),ITB(5,NB),ITP(5,NP)
*
*  Longitude
*                                         M   M'  D   F   n
      DATA TL( 1)/            +6.288750D0                     /,
     :     (ITL(I, 1),I=1,5)/            +0, +1, +0, +0,  0   /
      DATA TL( 2)/            +1.274018D0                     /,
     :     (ITL(I, 2),I=1,5)/            +0, -1, +2, +0,  0   /
      DATA TL( 3)/            +0.658309D0                     /,
     :     (ITL(I, 3),I=1,5)/            +0, +0, +2, +0,  0   /
      DATA TL( 4)/            +0.213616D0                     /,
     :     (ITL(I, 4),I=1,5)/            +0, +2, +0, +0,  0   /
      DATA TL( 5)/            -0.185596D0                     /,
     :     (ITL(I, 5),I=1,5)/            +1, +0, +0, +0,  1   /
      DATA TL( 6)/            -0.114336D0                     /,
     :     (ITL(I, 6),I=1,5)/            +0, +0, +0, +2,  0   /
      DATA TL( 7)/            +0.058793D0                     /,
     :     (ITL(I, 7),I=1,5)/            +0, -2, +2, +0,  0   /
      DATA TL( 8)/            +0.057212D0                     /,
     :     (ITL(I, 8),I=1,5)/            -1, -1, +2, +0,  1   /
      DATA TL( 9)/            +0.053320D0                     /,
     :     (ITL(I, 9),I=1,5)/            +0, +1, +2, +0,  0   /
      DATA TL(10)/            +0.045874D0                     /,
     :     (ITL(I,10),I=1,5)/            -1, +0, +2, +0,  1   /
      DATA TL(11)/            +0.041024D0                     /,
     :     (ITL(I,11),I=1,5)/            -1, +1, +0, +0,  1   /
      DATA TL(12)/            -0.034718D0                     /,
     :     (ITL(I,12),I=1,5)/            +0, +0, +1, +0,  0   /
      DATA TL(13)/            -0.030465D0                     /,
     :     (ITL(I,13),I=1,5)/            +1, +1, +0, +0,  1   /
      DATA TL(14)/            +0.015326D0                     /,
     :     (ITL(I,14),I=1,5)/            +0, +0, +2, -2,  0   /
      DATA TL(15)/            -0.012528D0                     /,
     :     (ITL(I,15),I=1,5)/            +0, +1, +0, +2,  0   /
      DATA TL(16)/            -0.010980D0                     /,
     :     (ITL(I,16),I=1,5)/            +0, -1, +0, +2,  0   /
      DATA TL(17)/            +0.010674D0                     /,
     :     (ITL(I,17),I=1,5)/            +0, -1, +4, +0,  0   /
      DATA TL(18)/            +0.010034D0                     /,
     :     (ITL(I,18),I=1,5)/            +0, +3, +0, +0,  0   /
      DATA TL(19)/            +0.008548D0                     /,
     :     (ITL(I,19),I=1,5)/            +0, -2, +4, +0,  0   /
      DATA TL(20)/            -0.007910D0                     /,
     :     (ITL(I,20),I=1,5)/            +1, -1, +2, +0,  1   /
      DATA TL(21)/            -0.006783D0                     /,
     :     (ITL(I,21),I=1,5)/            +1, +0, +2, +0,  1   /
      DATA TL(22)/            +0.005162D0                     /,
     :     (ITL(I,22),I=1,5)/            +0, +1, -1, +0,  0   /
      DATA TL(23)/            +0.005000D0                     /,
     :     (ITL(I,23),I=1,5)/            +1, +0, +1, +0,  1   /
      DATA TL(24)/            +0.004049D0                     /,
     :     (ITL(I,24),I=1,5)/            -1, +1, +2, +0,  1   /
      DATA TL(25)/            +0.003996D0                     /,
     :     (ITL(I,25),I=1,5)/            +0, +2, +2, +0,  0   /
      DATA TL(26)/            +0.003862D0                     /,
     :     (ITL(I,26),I=1,5)/            +0, +0, +4, +0,  0   /
      DATA TL(27)/            +0.003665D0                     /,
     :     (ITL(I,27),I=1,5)/            +0, -3, +2, +0,  0   /
      DATA TL(28)/            +0.002695D0                     /,
     :     (ITL(I,28),I=1,5)/            -1, +2, +0, +0,  1   /
      DATA TL(29)/            +0.002602D0                     /,
     :     (ITL(I,29),I=1,5)/            +0, +1, -2, -2,  0   /
      DATA TL(30)/            +0.002396D0                     /,
     :     (ITL(I,30),I=1,5)/            -1, -2, +2, +0,  1   /
      DATA TL(31)/            -0.002349D0                     /,
     :     (ITL(I,31),I=1,5)/            +0, +1, +1, +0,  0   /
      DATA TL(32)/            +0.002249D0                     /,
     :     (ITL(I,32),I=1,5)/            -2, +0, +2, +0,  2   /
      DATA TL(33)/            -0.002125D0                     /,
     :     (ITL(I,33),I=1,5)/            +1, +2, +0, +0,  1   /
      DATA TL(34)/            -0.002079D0                     /,
     :     (ITL(I,34),I=1,5)/            +2, +0, +0, +0,  2   /
      DATA TL(35)/            +0.002059D0                     /,
     :     (ITL(I,35),I=1,5)/            -2, -1, +2, +0,  2   /
      DATA TL(36)/            -0.001773D0                     /,
     :     (ITL(I,36),I=1,5)/            +0, +1, +2, -2,  0   /
      DATA TL(37)/            -0.001595D0                     /,
     :     (ITL(I,37),I=1,5)/            +0, +0, +2, +2,  0   /
      DATA TL(38)/            +0.001220D0                     /,
     :     (ITL(I,38),I=1,5)/            -1, -1, +4, +0,  1   /
      DATA TL(39)/            -0.001110D0                     /,
     :     (ITL(I,39),I=1,5)/            +0, +2, +0, +2,  0   /
      DATA TL(40)/            +0.000892D0                     /,
     :     (ITL(I,40),I=1,5)/            +0, +1, -3, +0,  0   /
      DATA TL(41)/            -0.000811D0                     /,
     :     (ITL(I,41),I=1,5)/            +1, +1, +2, +0,  1   /
      DATA TL(42)/            +0.000761D0                     /,
     :     (ITL(I,42),I=1,5)/            -1, -2, +4, +0,  1   /
      DATA TL(43)/            +0.000717D0                     /,
     :     (ITL(I,43),I=1,5)/            -2, +1, +0, +0,  2   /
      DATA TL(44)/            +0.000704D0                     /,
     :     (ITL(I,44),I=1,5)/            -2, +1, -2, +0,  2   /
      DATA TL(45)/            +0.000693D0                     /,
     :     (ITL(I,45),I=1,5)/            +1, -2, +2, +0,  1   /
      DATA TL(46)/            +0.000598D0                     /,
     :     (ITL(I,46),I=1,5)/            -1, +0, +2, -2,  1   /
      DATA TL(47)/            +0.000550D0                     /,
     :     (ITL(I,47),I=1,5)/            +0, +1, +4, +0,  0   /
      DATA TL(48)/            +0.000538D0                     /,
     :     (ITL(I,48),I=1,5)/            +0, +4, +0, +0,  0   /
      DATA TL(49)/            +0.000521D0                     /,
     :     (ITL(I,49),I=1,5)/            -1, +0, +4, +0,  1   /
      DATA TL(50)/            +0.000486D0                     /,
     :     (ITL(I,50),I=1,5)/            +0, +2, -1, +0,  0   /
*
*  Latitude
*                                         M   M'  D   F   n
      DATA TB( 1)/            +5.128189D0                     /,
     :     (ITB(I, 1),I=1,5)/            +0, +0, +0, +1,  0   /
      DATA TB( 2)/            +0.280606D0                     /,
     :     (ITB(I, 2),I=1,5)/            +0, +1, +0, +1,  0   /
      DATA TB( 3)/            +0.277693D0                     /,
     :     (ITB(I, 3),I=1,5)/            +0, +1, +0, -1,  0   /
      DATA TB( 4)/            +0.173238D0                     /,
     :     (ITB(I, 4),I=1,5)/            +0, +0, +2, -1,  0   /
      DATA TB( 5)/            +0.055413D0                     /,
     :     (ITB(I, 5),I=1,5)/            +0, -1, +2, +1,  0   /
      DATA TB( 6)/            +0.046272D0                     /,
     :     (ITB(I, 6),I=1,5)/            +0, -1, +2, -1,  0   /
      DATA TB( 7)/            +0.032573D0                     /,
     :     (ITB(I, 7),I=1,5)/            +0, +0, +2, +1,  0   /
      DATA TB( 8)/            +0.017198D0                     /,
     :     (ITB(I, 8),I=1,5)/            +0, +2, +0, +1,  0   /
      DATA TB( 9)/            +0.009267D0                     /,
     :     (ITB(I, 9),I=1,5)/            +0, +1, +2, -1,  0   /
      DATA TB(10)/            +0.008823D0                     /,
     :     (ITB(I,10),I=1,5)/            +0, +2, +0, -1,  0   /
      DATA TB(11)/            +0.008247D0                     /,
     :     (ITB(I,11),I=1,5)/            -1, +0, +2, -1,  1   /
      DATA TB(12)/            +0.004323D0                     /,
     :     (ITB(I,12),I=1,5)/            +0, -2, +2, -1,  0   /
      DATA TB(13)/            +0.004200D0                     /,
     :     (ITB(I,13),I=1,5)/            +0, +1, +2, +1,  0   /
      DATA TB(14)/            +0.003372D0                     /,
     :     (ITB(I,14),I=1,5)/            -1, +0, -2, +1,  1   /
      DATA TB(15)/            +0.002472D0                     /,
     :     (ITB(I,15),I=1,5)/            -1, -1, +2, +1,  1   /
      DATA TB(16)/            +0.002222D0                     /,
     :     (ITB(I,16),I=1,5)/            -1, +0, +2, +1,  1   /
      DATA TB(17)/            +0.002072D0                     /,
     :     (ITB(I,17),I=1,5)/            -1, -1, +2, -1,  1   /
      DATA TB(18)/            +0.001877D0                     /,
     :     (ITB(I,18),I=1,5)/            -1, +1, +0, +1,  1   /
      DATA TB(19)/            +0.001828D0                     /,
     :     (ITB(I,19),I=1,5)/            +0, -1, +4, -1,  0   /
      DATA TB(20)/            -0.001803D0                     /,
     :     (ITB(I,20),I=1,5)/            +1, +0, +0, +1,  1   /
      DATA TB(21)/            -0.001750D0                     /,
     :     (ITB(I,21),I=1,5)/            +0, +0, +0, +3,  0   /
      DATA TB(22)/            +0.001570D0                     /,
     :     (ITB(I,22),I=1,5)/            -1, +1, +0, -1,  1   /
      DATA TB(23)/            -0.001487D0                     /,
     :     (ITB(I,23),I=1,5)/            +0, +0, +1, +1,  0   /
      DATA TB(24)/            -0.001481D0                     /,
     :     (ITB(I,24),I=1,5)/            +1, +1, +0, +1,  1   /
      DATA TB(25)/            +0.001417D0                     /,
     :     (ITB(I,25),I=1,5)/            -1, -1, +0, +1,  1   /
      DATA TB(26)/            +0.001350D0                     /,
     :     (ITB(I,26),I=1,5)/            -1, +0, +0, +1,  1   /
      DATA TB(27)/            +0.001330D0                     /,
     :     (ITB(I,27),I=1,5)/            +0, +0, -1, +1,  0   /
      DATA TB(28)/            +0.001106D0                     /,
     :     (ITB(I,28),I=1,5)/            +0, +3, +0, +1,  0   /
      DATA TB(29)/            +0.001020D0                     /,
     :     (ITB(I,29),I=1,5)/            +0, +0, +4, -1,  0   /
      DATA TB(30)/            +0.000833D0                     /,
     :     (ITB(I,30),I=1,5)/            +0, -1, +4, +1,  0   /
      DATA TB(31)/            +0.000781D0                     /,
     :     (ITB(I,31),I=1,5)/            +0, +1, +0, -3,  0   /
      DATA TB(32)/            +0.000670D0                     /,
     :     (ITB(I,32),I=1,5)/            +0, -2, +4, +1,  0   /
      DATA TB(33)/            +0.000606D0                     /,
     :     (ITB(I,33),I=1,5)/            +0, +0, +2, -3,  0   /
      DATA TB(34)/            +0.000597D0                     /,
     :     (ITB(I,34),I=1,5)/            +0, +2, +2, -1,  0   /
      DATA TB(35)/            +0.000492D0                     /,
     :     (ITB(I,35),I=1,5)/            -1, +1, +2, -1,  1   /
      DATA TB(36)/            +0.000450D0                     /,
     :     (ITB(I,36),I=1,5)/            +0, +2, -2, -1,  0   /
      DATA TB(37)/            +0.000439D0                     /,
     :     (ITB(I,37),I=1,5)/            +0, +3, +0, -1,  0   /
      DATA TB(38)/            +0.000423D0                     /,
     :     (ITB(I,38),I=1,5)/            +0, +2, +2, +1,  0   /
      DATA TB(39)/            +0.000422D0                     /,
     :     (ITB(I,39),I=1,5)/            +0, -3, +2, -1,  0   /
      DATA TB(40)/            -0.000367D0                     /,
     :     (ITB(I,40),I=1,5)/            +1, -1, +2, +1,  1   /
      DATA TB(41)/            -0.000353D0                     /,
     :     (ITB(I,41),I=1,5)/            +1, +0, +2, +1,  1   /
      DATA TB(42)/            +0.000331D0                     /,
     :     (ITB(I,42),I=1,5)/            +0, +0, +4, +1,  0   /
      DATA TB(43)/            +0.000317D0                     /,
     :     (ITB(I,43),I=1,5)/            -1, +1, +2, +1,  1   /
      DATA TB(44)/            +0.000306D0                     /,
     :     (ITB(I,44),I=1,5)/            -2, +0, +2, -1,  2   /
      DATA TB(45)/            -0.000283D0                     /,
     :     (ITB(I,45),I=1,5)/            +0, +1, +0, +3,  0   /
*
*  Parallax
*                                         M   M'  D   F   n
      DATA TP( 1)/            +0.950724D0                     /,
     :     (ITP(I, 1),I=1,5)/            +0, +0, +0, +0,  0   /
      DATA TP( 2)/            +0.051818D0                     /,
     :     (ITP(I, 2),I=1,5)/            +0, +1, +0, +0,  0   /
      DATA TP( 3)/            +0.009531D0                     /,
     :     (ITP(I, 3),I=1,5)/            +0, -1, +2, +0,  0   /
      DATA TP( 4)/            +0.007843D0                     /,
     :     (ITP(I, 4),I=1,5)/            +0, +0, +2, +0,  0   /
      DATA TP( 5)/            +0.002824D0                     /,
     :     (ITP(I, 5),I=1,5)/            +0, +2, +0, +0,  0   /
      DATA TP( 6)/            +0.000857D0                     /,
     :     (ITP(I, 6),I=1,5)/            +0, +1, +2, +0,  0   /
      DATA TP( 7)/            +0.000533D0                     /,
     :     (ITP(I, 7),I=1,5)/            -1, +0, +2, +0,  1   /
      DATA TP( 8)/            +0.000401D0                     /,
     :     (ITP(I, 8),I=1,5)/            -1, -1, +2, +0,  1   /
      DATA TP( 9)/            +0.000320D0                     /,
     :     (ITP(I, 9),I=1,5)/            -1, +1, +0, +0,  1   /
      DATA TP(10)/            -0.000271D0                     /,
     :     (ITP(I,10),I=1,5)/            +0, +0, +1, +0,  0   /
      DATA TP(11)/            -0.000264D0                     /,
     :     (ITP(I,11),I=1,5)/            +1, +1, +0, +0,  1   /
      DATA TP(12)/            -0.000198D0                     /,
     :     (ITP(I,12),I=1,5)/            +0, -1, +0, +2,  0   /
      DATA TP(13)/            +0.000173D0                     /,
     :     (ITP(I,13),I=1,5)/            +0, +3, +0, +0,  0   /
      DATA TP(14)/            +0.000167D0                     /,
     :     (ITP(I,14),I=1,5)/            +0, -1, +4, +0,  0   /
      DATA TP(15)/            -0.000111D0                     /,
     :     (ITP(I,15),I=1,5)/            +1, +0, +0, +0,  1   /
      DATA TP(16)/            +0.000103D0                     /,
     :     (ITP(I,16),I=1,5)/            +0, -2, +4, +0,  0   /
      DATA TP(17)/            -0.000084D0                     /,
     :     (ITP(I,17),I=1,5)/            +0, +2, -2, +0,  0   /
      DATA TP(18)/            -0.000083D0                     /,
     :     (ITP(I,18),I=1,5)/            +1, +0, +2, +0,  1   /
      DATA TP(19)/            +0.000079D0                     /,
     :     (ITP(I,19),I=1,5)/            +0, +2, +2, +0,  0   /
      DATA TP(20)/            +0.000072D0                     /,
     :     (ITP(I,20),I=1,5)/            +0, +0, +4, +0,  0   /
      DATA TP(21)/            +0.000064D0                     /,
     :     (ITP(I,21),I=1,5)/            -1, +1, +2, +0,  1   /
      DATA TP(22)/            -0.000063D0                     /,
     :     (ITP(I,22),I=1,5)/            +1, -1, +2, +0,  1   /
      DATA TP(23)/            +0.000041D0                     /,
     :     (ITP(I,23),I=1,5)/            +1, +0, +1, +0,  1   /
      DATA TP(24)/            +0.000035D0                     /,
     :     (ITP(I,24),I=1,5)/            -1, +2, +0, +0,  1   /
      DATA TP(25)/            -0.000033D0                     /,
     :     (ITP(I,25),I=1,5)/            +0, +3, -2, +0,  0   /
      DATA TP(26)/            -0.000030D0                     /,
     :     (ITP(I,26),I=1,5)/            +0, +1, +1, +0,  0   /
      DATA TP(27)/            -0.000029D0                     /,
     :     (ITP(I,27),I=1,5)/            +0, +0, -2, +2,  0   /
      DATA TP(28)/            -0.000029D0                     /,
     :     (ITP(I,28),I=1,5)/            +1, +2, +0, +0,  1   /
      DATA TP(29)/            +0.000026D0                     /,
     :     (ITP(I,29),I=1,5)/            -2, +0, +2, +0,  2   /
      DATA TP(30)/            -0.000023D0                     /,
     :     (ITP(I,30),I=1,5)/            +0, +1, -2, +2,  0   /
      DATA TP(31)/            +0.000019D0                     /,
     :     (ITP(I,31),I=1,5)/            -1, -1, +4, +0,  1   /



*  Centuries since J1900
      T=(DATE-15019.5D0)/36525D0

*
*  Fundamental arguments (radians) and derivatives (radians per
*  Julian century) for the current epoch
*

*  Moon's mean longitude
      ELP=D2R*MOD(ELP0+(ELP1+(ELP2+ELP3*T)*T)*T,360D0)
      DELP=D2R*(ELP1+(2D0*ELP2+3D0*ELP3*T)*T)

*  Sun's mean anomaly
      EM=D2R*MOD(EM0+(EM1+(EM2+EM3*T)*T)*T,360D0)
      DEM=D2R*(EM1+(2D0*EM2+3D0*EM3*T)*T)

*  Moon's mean anomaly
      EMP=D2R*MOD(EMP0+(EMP1+(EMP2+EMP3*T)*T)*T,360D0)
      DEMP=D2R*(EMP1+(2D0*EMP2+3D0*EMP3*T)*T)

*  Moon's mean elongation
      D=D2R*MOD(D0+(D1+(D2+D3*T)*T)*T,360D0)
      DD=D2R*(D1+(2D0*D2+3D0*D3*T)*T)

*  Mean distance of the Moon from its ascending node
      F=D2R*MOD(F0+(F1+(F2+F3*T)*T)*T,360D0)
      DF=D2R*(F1+(2D0*F2+3D0*F3*T)*T)

*  Longitude of the Moon's ascending node
      OM=D2R*MOD(OM0+(OM1+(OM2+OM3*T)*T)*T,360D0)
      DOM=D2R*(OM1+(2D0*OM2+3D0*OM3*T)*T)
      SINOM=SIN(OM)
      COSOM=COS(OM)
      DOMCOM=DOM*COSOM

*  Add the periodic variations
      THETA=D2R*(PA0+PA1*T)
      WA=SIN(THETA)
      DWA=D2R*PA1*COS(THETA)
      THETA=D2R*(PE0+(PE1+PE2*T)*T)
      WB=PEC*SIN(THETA)
      DWB=D2R*PEC*(PE1+2D0*PE2*T)*COS(THETA)
      ELP=ELP+D2R*(PAC*WA+WB+PFC*SINOM)
      DELP=DELP+D2R*(PAC*DWA+DWB+PFC*DOMCOM)
      EM=EM+D2R*PBC*WA
      DEM=DEM+D2R*PBC*DWA
      EMP=EMP+D2R*(PCC*WA+WB+PGC*SINOM)
      DEMP=DEMP+D2R*(PCC*DWA+DWB+PGC*DOMCOM)
      D=D+D2R*(PDC*WA+WB+PHC*SINOM)
      DD=DD+D2R*(PDC*DWA+DWB+PHC*DOMCOM)
      WOM=OM+D2R*(PJ0+PJ1*T)
      DWOM=DOM+D2R*PJ1
      SINWOM=SIN(WOM)
      COSWOM=COS(WOM)
      F=F+D2R*(WB+PIC*SINOM+PJC*SINWOM)
      DF=DF+D2R*(DWB+PIC*DOMCOM+PJC*DWOM*COSWOM)

*  E-factor, and square
      E=1D0+(E1+E2*T)*T
      DE=E1+2D0*E2*T
      ESQ=E*E
      DESQ=2D0*E*DE

*
*  Series expansions
*

*  Longitude
      V=0D0
      DV=0D0
      DO N=NL,1,-1
         COEFF=TL(N)
         EMN=DBLE(ITL(1,N))
         EMPN=DBLE(ITL(2,N))
         DN=DBLE(ITL(3,N))
         FN=DBLE(ITL(4,N))
         I=ITL(5,N)
         IF (I.EQ.0) THEN
            EN=1D0
            DEN=0D0
         ELSE IF (I.EQ.1) THEN
            EN=E
            DEN=DE
         ELSE
            EN=ESQ
            DEN=DESQ
         END IF
         THETA=EMN*EM+EMPN*EMP+DN*D+FN*F
         DTHETA=EMN*DEM+EMPN*DEMP+DN*DD+FN*DF
         FTHETA=SIN(THETA)
         V=V+COEFF*FTHETA*EN
         DV=DV+COEFF*(COS(THETA)*DTHETA*EN+FTHETA*DEN)
      END DO
      EL=ELP+D2R*V
      DEL=(DELP+D2R*DV)/CJ

*  Latitude
      V=0D0
      DV=0D0
      DO N=NB,1,-1
         COEFF=TB(N)
         EMN=DBLE(ITB(1,N))
         EMPN=DBLE(ITB(2,N))
         DN=DBLE(ITB(3,N))
         FN=DBLE(ITB(4,N))
         I=ITB(5,N)
         IF (I.EQ.0) THEN
            EN=1D0
            DEN=0D0
         ELSE IF (I.EQ.1) THEN
            EN=E
            DEN=DE
         ELSE
            EN=ESQ
            DEN=DESQ
         END IF
         THETA=EMN*EM+EMPN*EMP+DN*D+FN*F
         DTHETA=EMN*DEM+EMPN*DEMP+DN*DD+FN*DF
         FTHETA=SIN(THETA)
         V=V+COEFF*FTHETA*EN
         DV=DV+COEFF*(COS(THETA)*DTHETA*EN+FTHETA*DEN)
      END DO
      BF=1D0-CW1*COSOM-CW2*COSWOM
      DBF=CW1*DOM*SINOM+CW2*DWOM*SINWOM
      B=D2R*V*BF
      DB=D2R*(DV*BF+V*DBF)/CJ

*  Parallax
      V=0D0
      DV=0D0
      DO N=NP,1,-1
         COEFF=TP(N)
         EMN=DBLE(ITP(1,N))
         EMPN=DBLE(ITP(2,N))
         DN=DBLE(ITP(3,N))
         FN=DBLE(ITP(4,N))
         I=ITP(5,N)
         IF (I.EQ.0) THEN
            EN=1D0
            DEN=0D0
         ELSE IF (I.EQ.1) THEN
            EN=E
            DEN=DE
         ELSE
            EN=ESQ
            DEN=DESQ
         END IF
         THETA=EMN*EM+EMPN*EMP+DN*D+FN*F
         DTHETA=EMN*DEM+EMPN*DEMP+DN*DD+FN*DF
         FTHETA=COS(THETA)
         V=V+COEFF*FTHETA*EN
         DV=DV+COEFF*(-SIN(THETA)*DTHETA*EN+FTHETA*DEN)
      END DO
      P=D2R*V
      DP=D2R*DV/CJ

*
*  Transformation into final form
*

*  Parallax to distance (AU, AU/sec)
      SP=SIN(P)
      R=ERADAU/SP
      DR=-R*DP*COS(P)/SP

*  Longitude, latitude to x,y,z (AU)
      SEL=SIN(EL)
      CEL=COS(EL)
      SB=SIN(B)
      CB=COS(B)
      RCB=R*CB
      RBD=R*DB
      W=RBD*SB-CB*DR
      X=RCB*CEL
      Y=RCB*SEL
      Z=R*SB
      XD=-Y*DEL-W*CEL
      YD=X*DEL-W*SEL
      ZD=RBD*CB+SB*DR

*  Julian centuries since J2000
      T=(DATE-51544.5D0)/36525D0

*  Fricke equinox correction
      EPJ=2000D0+T*100D0
      EQCOR=DS2R*(0.035D0+0.00085D0*(EPJ-B1950))

*  Mean obliquity (IAU 1976)
      EPS=DAS2R*(84381.448D0+(-46.8150D0+(-0.00059D0+0.001813D0*T)*T)*T)

*  To the equatorial system, mean of date, FK5 system
      SINEPS=SIN(EPS)
      COSEPS=COS(EPS)
      ES=EQCOR*SINEPS
      EC=EQCOR*COSEPS
      PV(1)=X-EC*Y+ES*Z
      PV(2)=EQCOR*X+Y*COSEPS-Z*SINEPS
      PV(3)=Y*SINEPS+Z*COSEPS
      PV(4)=XD-EC*YD+ES*ZD
      PV(5)=EQCOR*XD+YD*COSEPS-ZD*SINEPS
      PV(6)=YD*SINEPS+ZD*COSEPS

      END
      SUBROUTINE sla_DMXV (DM, VA, VB)
*+
*     - - - - -
*      D M X V
*     - - - - -
*
*  Performs the 3-D forward unitary transformation:
*
*     vector VB = matrix DM * vector VA
*
*  (double precision)
*
*  Given:
*     DM       dp(3,3)    matrix
*     VA       dp(3)      vector
*
*  Returned:
*     VB       dp(3)      result vector
*
*  To comply with the ANSI Fortran 77 standard, VA and VB must be
*  different arrays.  However, the routine is coded so as to work
*  properly on many platforms even if this rule is violated.
*
*  Last revision:   26 December 2004
*
*  Copyright P.T.Wallace.  All rights reserved.
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION DM(3,3),VA(3),VB(3)

      INTEGER I,J
      DOUBLE PRECISION W,VW(3)


*  Matrix DM * vector VA -> vector VW
      DO J=1,3
         W=0D0
         DO I=1,3
            W=W+DM(J,I)*VA(I)
         END DO
         VW(J)=W
      END DO

*  Vector VW -> vector VB
      DO J=1,3
         VB(J)=VW(J)
      END DO

      END
      DOUBLE PRECISION FUNCTION sla_DRANGE (ANGLE)
*+
*     - - - - - - -
*      D R A N G E
*     - - - - - - -
*
*  Normalize angle into range +/- pi  (double precision)
*
*  Given:
*     ANGLE     dp      the angle in radians
*
*  The result (double precision) is ANGLE expressed in the range +/- pi.
*
*  P.T.Wallace   Starlink   23 November 1995
*
*  Copyright (C) 1995 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION ANGLE

      DOUBLE PRECISION DPI,D2PI
      PARAMETER (DPI=3.141592653589793238462643D0)
      PARAMETER (D2PI=6.283185307179586476925287D0)


      sla_DRANGE=MOD(ANGLE,D2PI)
      IF (ABS(sla_DRANGE).GE.DPI)
     :          sla_DRANGE=sla_DRANGE-SIGN(D2PI,ANGLE)

      END
      DOUBLE PRECISION FUNCTION sla_DRANRM (ANGLE)
*+
*     - - - - - - -
*      D R A N R M
*     - - - - - - -
*
*  Normalize angle into range 0-2 pi  (double precision)
*
*  Given:
*     ANGLE     dp      the angle in radians
*
*  The result is ANGLE expressed in the range 0-2 pi.
*
*  Last revision:   22 July 2004
*
*  Copyright P.T.Wallace.  All rights reserved.
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION ANGLE

      DOUBLE PRECISION D2PI
      PARAMETER (D2PI=6.283185307179586476925286766559D0)


      sla_DRANRM = MOD(ANGLE,D2PI)
      IF (sla_DRANRM.LT.0D0) sla_DRANRM = sla_DRANRM+D2PI

      END
      DOUBLE PRECISION FUNCTION sla_DTT (UTC)
*+
*     - - - -
*      D T T
*     - - - -
*
*  Increment to be applied to Coordinated Universal Time UTC to give
*  Terrestrial Time TT (formerly Ephemeris Time ET)
*
*  (double precision)
*
*  Given:
*     UTC      d      UTC date as a modified JD (JD-2400000.5)
*
*  Result:  TT-UTC in seconds
*
*  Notes:
*
*  1  The UTC is specified to be a date rather than a time to indicate
*     that care needs to be taken not to specify an instant which lies
*     within a leap second.  Though in most cases UTC can include the
*     fractional part, correct behaviour on the day of a leap second
*     can only be guaranteed up to the end of the second 23:59:59.
*
*  2  Pre 1972 January 1 a fixed value of 10 + ET-TAI is returned.
*
*  3  See also the routine sla_DT, which roughly estimates ET-UT for
*     historical epochs.
*
*  Called:  sla_DAT
*
*  P.T.Wallace   Starlink   6 December 1994
*
*  Copyright (C) 1995 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION UTC

      DOUBLE PRECISION sla_DAT


      sla_DTT=32.184D0+sla_DAT(UTC)

      END
      SUBROUTINE sla_ECMAT (DATE, RMAT)
*+
*     - - - - - -
*      E C M A T
*     - - - - - -
*
*  Form the equatorial to ecliptic rotation matrix - IAU 1980 theory
*  (double precision)
*
*  Given:
*     DATE     dp         TDB (loosely ET) as Modified Julian Date
*                                            (JD-2400000.5)
*  Returned:
*     RMAT     dp(3,3)    matrix
*
*  Reference:
*     Murray,C.A., Vectorial Astrometry, section 4.3.
*
*  Note:
*    The matrix is in the sense   V(ecl)  =  RMAT * V(equ);  the
*    equator, equinox and ecliptic are mean of date.
*
*  Called:  sla_DEULER
*
*  P.T.Wallace   Starlink   23 August 1996
*
*  Copyright (C) 1996 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION DATE,RMAT(3,3)

*  Arc seconds to radians
      DOUBLE PRECISION AS2R
      PARAMETER (AS2R=0.484813681109535994D-5)

      DOUBLE PRECISION T,EPS0



*  Interval between basic epoch J2000.0 and current epoch (JC)
      T = (DATE-51544.5D0)/36525D0

*  Mean obliquity
      EPS0 = AS2R*
     :   (84381.448D0+(-46.8150D0+(-0.00059D0+0.001813D0*T)*T)*T)

*  Matrix
      CALL sla_DEULER('X',EPS0,0D0,0D0,RMAT)

      END
      DOUBLE PRECISION FUNCTION sla_EPJ (DATE)
*+
*     - - - -
*      E P J
*     - - - -
*
*  Conversion of Modified Julian Date to Julian Epoch (double precision)
*
*  Given:
*     DATE     dp       Modified Julian Date (JD - 2400000.5)
*
*  The result is the Julian Epoch.
*
*  Reference:
*     Lieske,J.H., 1979. Astron.Astrophys.,73,282.
*
*  P.T.Wallace   Starlink   February 1984
*
*  Copyright (C) 1995 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION DATE


      sla_EPJ = 2000D0 + (DATE-51544.5D0)/365.25D0

      END
      SUBROUTINE sla_EQECL (DR, DD, DATE, DL, DB)
*+
*     - - - - - -
*      E Q E C L
*     - - - - - -
*
*  Transformation from J2000.0 equatorial coordinates to
*  ecliptic coordinates (double precision)
*
*  Given:
*     DR,DD       dp      J2000.0 mean RA,Dec (radians)
*     DATE        dp      TDB (loosely ET) as Modified Julian Date
*                                              (JD-2400000.5)
*  Returned:
*     DL,DB       dp      ecliptic longitude and latitude
*                         (mean of date, IAU 1980 theory, radians)
*
*  Called:
*     sla_DCS2C, sla_PREC, sla_EPJ, sla_DMXV, sla_ECMAT, sla_DCC2S,
*     sla_DRANRM, sla_DRANGE
*
*  P.T.Wallace   Starlink   March 1986
*
*  Copyright (C) 1995 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION DR,DD,DATE,DL,DB

      DOUBLE PRECISION sla_EPJ,sla_DRANRM,sla_DRANGE

      DOUBLE PRECISION RMAT(3,3),V1(3),V2(3)



*  Spherical to Cartesian
      CALL sla_DCS2C(DR,DD,V1)

*  Mean J2000 to mean of date
      CALL sla_PREC(2000D0,sla_EPJ(DATE),RMAT)
      CALL sla_DMXV(RMAT,V1,V2)

*  Equatorial to ecliptic
      CALL sla_ECMAT(DATE,RMAT)
      CALL sla_DMXV(RMAT,V2,V1)

*  Cartesian to spherical
      CALL sla_DCC2S(V1,DL,DB)

*  Express in conventional ranges
      DL=sla_DRANRM(DL)
      DB=sla_DRANGE(DB)

      END
      DOUBLE PRECISION FUNCTION sla_EQEQX (DATE)
*+
*     - - - - - -
*      E Q E Q X
*     - - - - - -
*
*  Equation of the equinoxes  (IAU 1994, double precision)
*
*  Given:
*     DATE    dp      TDB (loosely ET) as Modified Julian Date
*                                          (JD-2400000.5)
*
*  The result is the equation of the equinoxes (double precision)
*  in radians:
*
*     Greenwich apparent ST = GMST + sla_EQEQX
*
*  References:  IAU Resolution C7, Recommendation 3 (1994)
*               Capitaine, N. & Gontier, A.-M., Astron. Astrophys.,
*               275, 645-650 (1993)
*
*  Called:  sla_NUTC
*
*  Patrick Wallace   Starlink   23 August 1996
*
*  Copyright (C) 1996 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION DATE

*  Turns to arc seconds and arc seconds to radians
      DOUBLE PRECISION T2AS,AS2R
      PARAMETER (T2AS=1296000D0,
     :           AS2R=0.484813681109535994D-5)

      DOUBLE PRECISION T,OM,DPSI,DEPS,EPS0



*  Interval between basic epoch J2000.0 and current epoch (JC)
      T=(DATE-51544.5D0)/36525D0

*  Longitude of the mean ascending node of the lunar orbit on the
*   ecliptic, measured from the mean equinox of date
      OM=AS2R*(450160.280D0+(-5D0*T2AS-482890.539D0
     :         +(7.455D0+0.008D0*T)*T)*T)

*  Nutation
      CALL sla_NUTC(DATE,DPSI,DEPS,EPS0)

*  Equation of the equinoxes
      sla_EQEQX=DPSI*COS(EPS0)+AS2R*(0.00264D0*SIN(OM)+
     :                               0.000063D0*SIN(OM+OM))

      END
      SUBROUTINE sla_GEOC (P, H, R, Z)
*+
*     - - - - -
*      G E O C
*     - - - - -
*
*  Convert geodetic position to geocentric (double precision)
*
*  Given:
*     P     dp     latitude (geodetic, radians)
*     H     dp     height above reference spheroid (geodetic, metres)
*
*  Returned:
*     R     dp     distance from Earth axis (AU)
*     Z     dp     distance from plane of Earth equator (AU)
*
*  Notes:
*
*  1  Geocentric latitude can be obtained by evaluating ATAN2(Z,R).
*
*  2  IAU 1976 constants are used.
*
*  Reference:
*
*     Green,R.M., Spherical Astronomy, CUP 1985, p98.
*
*  Last revision:   22 July 2004
*
*  Copyright P.T.Wallace.  All rights reserved.
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION P,H,R,Z

*  Earth equatorial radius (metres)
      DOUBLE PRECISION A0
      PARAMETER (A0=6378140D0)

*  Reference spheroid flattening factor and useful function
      DOUBLE PRECISION F,B
      PARAMETER (F=1D0/298.257D0,B=(1D0-F)**2)

*  Astronomical unit in metres
      DOUBLE PRECISION AU
      PARAMETER (AU=1.49597870D11)

      DOUBLE PRECISION SP,CP,C,S



*  Geodetic to geocentric conversion
      SP = SIN(P)
      CP = COS(P)
      C = 1D0/SQRT(CP*CP+B*SP*SP)
      S = B*C
      R = (A0*C+H)*CP/AU
      Z = (A0*S+H)*SP/AU

      END
      DOUBLE PRECISION FUNCTION sla_GMST (UT1)
*+
*     - - - - -
*      G M S T
*     - - - - -
*
*  Conversion from universal time to sidereal time (double precision)
*
*  Given:
*    UT1    dp     universal time (strictly UT1) expressed as
*                  modified Julian Date (JD-2400000.5)
*
*  The result is the Greenwich mean sidereal time (double
*  precision, radians).
*
*  The IAU 1982 expression (see page S15 of 1984 Astronomical Almanac)
*  is used, but rearranged to reduce rounding errors.  This expression
*  is always described as giving the GMST at 0 hours UT.  In fact, it
*  gives the difference between the GMST and the UT, which happens to
*  equal the GMST (modulo 24 hours) at 0 hours UT each day.  In this
*  routine, the entire UT is used directly as the argument for the
*  standard formula, and the fractional part of the UT is added
*  separately.  Note that the factor 1.0027379... does not appear in the
*  IAU 1982 expression explicitly but in the form of the coefficient
*  8640184.812866, which is 86400x36525x0.0027379...
*
*  See also the routine sla_GMSTA, which delivers better numerical
*  precision by accepting the UT date and time as separate arguments.
*
*  Called:  sla_DRANRM
*
*  P.T.Wallace   Starlink   14 October 2001
*
*  Copyright (C) 2001 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION UT1

      DOUBLE PRECISION sla_DRANRM

      DOUBLE PRECISION D2PI,S2R
      PARAMETER (D2PI=6.283185307179586476925286766559D0,
     :           S2R=7.272205216643039903848711535369D-5)

      DOUBLE PRECISION TU



*  Julian centuries from fundamental epoch J2000 to this UT
      TU=(UT1-51544.5D0)/36525D0

*  GMST at this UT
      sla_GMST=sla_DRANRM(MOD(UT1,1D0)*D2PI+
     :                    (24110.54841D0+
     :                    (8640184.812866D0+
     :                    (0.093104D0-6.2D-6*TU)*TU)*TU)*S2R)

      END
      SUBROUTINE sla_NUTC (DATE, DPSI, DEPS, EPS0)
*+
*     - - - - -
*      N U T C
*     - - - - -
*
*  Nutation:  longitude & obliquity components and mean obliquity,
*  using the Shirai & Fukushima (2001) theory.
*
*  Given:
*     DATE        d    TDB (loosely ET) as Modified Julian Date
*                                            (JD-2400000.5)
*  Returned:
*     DPSI,DEPS   d    nutation in longitude,obliquity
*     EPS0        d    mean obliquity
*
*  Notes:
*
*  1  The routine predicts forced nutation (but not free core nutation)
*     plus corrections to the IAU 1976 precession model.
*
*  2  Earth attitude predictions made by combining the present nutation
*     model with IAU 1976 precession are accurate to 1 mas (with respect
*     to the ICRF) for a few decades around 2000.
*
*  3  The sla_NUTC80 routine is the equivalent of the present routine
*     but using the IAU 1980 nutation theory.  The older theory is less
*     accurate, leading to errors as large as 350 mas over the interval
*     1900-2100, mainly because of the error in the IAU 1976 precession.
*
*  References:
*
*     Shirai, T. & Fukushima, T., Astron.J. 121, 3270-3283 (2001).
*
*     Fukushima, T., Astron.Astrophys. 244, L11 (1991).
*
*     Simon, J. L., Bretagnon, P., Chapront, J., Chapront-Touze, M.,
*     Francou, G. & Laskar, J., Astron.Astrophys. 282, 663 (1994).
*
*  This revision:   24 November 2005
*
*  Copyright P.T.Wallace.  All rights reserved.
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION DATE,DPSI,DEPS,EPS0

*  Degrees to radians
      DOUBLE PRECISION DD2R
      PARAMETER (DD2R=1.745329251994329576923691D-2)

*  Arc seconds to radians
      DOUBLE PRECISION DAS2R
      PARAMETER (DAS2R=4.848136811095359935899141D-6)

*  Arc seconds in a full circle
      DOUBLE PRECISION TURNAS
      PARAMETER (TURNAS=1296000D0)

*  Reference epoch (J2000), MJD
      DOUBLE PRECISION DJM0
      PARAMETER (DJM0=51544.5D0 )

*  Days per Julian century
      DOUBLE PRECISION DJC
      PARAMETER (DJC=36525D0)

      INTEGER I,J
      DOUBLE PRECISION T,EL,ELP,F,D,OM,VE,MA,JU,SA,THETA,C,S,DP,DE

*  Number of terms in the nutation model
      INTEGER NTERMS
      PARAMETER (NTERMS=194)

*  The SF2001 forced nutation model
      INTEGER NA(9,NTERMS)
      DOUBLE PRECISION PSI(4,NTERMS), EPS(4,NTERMS)

*  Coefficients of fundamental angles
      DATA ( ( NA(I,J), I=1,9 ), J=1,10 ) /
     :    0,   0,   0,   0,  -1,   0,   0,   0,   0,
     :    0,   0,   2,  -2,   2,   0,   0,   0,   0,
     :    0,   0,   2,   0,   2,   0,   0,   0,   0,
     :    0,   0,   0,   0,  -2,   0,   0,   0,   0,
     :    0,   1,   0,   0,   0,   0,   0,   0,   0,
     :    0,   1,   2,  -2,   2,   0,   0,   0,   0,
     :    1,   0,   0,   0,   0,   0,   0,   0,   0,
     :    0,   0,   2,   0,   1,   0,   0,   0,   0,
     :    1,   0,   2,   0,   2,   0,   0,   0,   0,
     :    0,  -1,   2,  -2,   2,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=11,20 ) /
     :    0,   0,   2,  -2,   1,   0,   0,   0,   0,
     :   -1,   0,   2,   0,   2,   0,   0,   0,   0,
     :   -1,   0,   0,   2,   0,   0,   0,   0,   0,
     :    1,   0,   0,   0,   1,   0,   0,   0,   0,
     :    1,   0,   0,   0,  -1,   0,   0,   0,   0,
     :   -1,   0,   2,   2,   2,   0,   0,   0,   0,
     :    1,   0,   2,   0,   1,   0,   0,   0,   0,
     :   -2,   0,   2,   0,   1,   0,   0,   0,   0,
     :    0,   0,   0,   2,   0,   0,   0,   0,   0,
     :    0,   0,   2,   2,   2,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=21,30 ) /
     :    2,   0,   0,  -2,   0,   0,   0,   0,   0,
     :    2,   0,   2,   0,   2,   0,   0,   0,   0,
     :    1,   0,   2,  -2,   2,   0,   0,   0,   0,
     :   -1,   0,   2,   0,   1,   0,   0,   0,   0,
     :    2,   0,   0,   0,   0,   0,   0,   0,   0,
     :    0,   0,   2,   0,   0,   0,   0,   0,   0,
     :    0,   1,   0,   0,   1,   0,   0,   0,   0,
     :   -1,   0,   0,   2,   1,   0,   0,   0,   0,
     :    0,   2,   2,  -2,   2,   0,   0,   0,   0,
     :    0,   0,   2,  -2,   0,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=31,40 ) /
     :   -1,   0,   0,   2,  -1,   0,   0,   0,   0,
     :    0,   1,   0,   0,  -1,   0,   0,   0,   0,
     :    0,   2,   0,   0,   0,   0,   0,   0,   0,
     :   -1,   0,   2,   2,   1,   0,   0,   0,   0,
     :    1,   0,   2,   2,   2,   0,   0,   0,   0,
     :    0,   1,   2,   0,   2,   0,   0,   0,   0,
     :   -2,   0,   2,   0,   0,   0,   0,   0,   0,
     :    0,   0,   2,   2,   1,   0,   0,   0,   0,
     :    0,  -1,   2,   0,   2,   0,   0,   0,   0,
     :    0,   0,   0,   2,   1,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=41,50 ) /
     :    1,   0,   2,  -2,   1,   0,   0,   0,   0,
     :    2,   0,   0,  -2,  -1,   0,   0,   0,   0,
     :    2,   0,   2,  -2,   2,   0,   0,   0,   0,
     :    2,   0,   2,   0,   1,   0,   0,   0,   0,
     :    0,   0,   0,   2,  -1,   0,   0,   0,   0,
     :    0,  -1,   2,  -2,   1,   0,   0,   0,   0,
     :   -1,  -1,   0,   2,   0,   0,   0,   0,   0,
     :    2,   0,   0,  -2,   1,   0,   0,   0,   0,
     :    1,   0,   0,   2,   0,   0,   0,   0,   0,
     :    0,   1,   2,  -2,   1,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=51,60 ) /
     :    1,  -1,   0,   0,   0,   0,   0,   0,   0,
     :   -2,   0,   2,   0,   2,   0,   0,   0,   0,
     :    0,  -1,   0,   2,   0,   0,   0,   0,   0,
     :    3,   0,   2,   0,   2,   0,   0,   0,   0,
     :    0,   0,   0,   1,   0,   0,   0,   0,   0,
     :    1,  -1,   2,   0,   2,   0,   0,   0,   0,
     :    1,   0,   0,  -1,   0,   0,   0,   0,   0,
     :   -1,  -1,   2,   2,   2,   0,   0,   0,   0,
     :   -1,   0,   2,   0,   0,   0,   0,   0,   0,
     :    2,   0,   0,   0,  -1,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=61,70 ) /
     :    0,  -1,   2,   2,   2,   0,   0,   0,   0,
     :    1,   1,   2,   0,   2,   0,   0,   0,   0,
     :    2,   0,   0,   0,   1,   0,   0,   0,   0,
     :    1,   1,   0,   0,   0,   0,   0,   0,   0,
     :    1,   0,  -2,   2,  -1,   0,   0,   0,   0,
     :    1,   0,   2,   0,   0,   0,   0,   0,   0,
     :   -1,   1,   0,   1,   0,   0,   0,   0,   0,
     :    1,   0,   0,   0,   2,   0,   0,   0,   0,
     :   -1,   0,   1,   0,   1,   0,   0,   0,   0,
     :    0,   0,   2,   1,   2,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=71,80 ) /
     :   -1,   1,   0,   1,   1,   0,   0,   0,   0,
     :   -1,   0,   2,   4,   2,   0,   0,   0,   0,
     :    0,  -2,   2,  -2,   1,   0,   0,   0,   0,
     :    1,   0,   2,   2,   1,   0,   0,   0,   0,
     :    1,   0,   0,   0,  -2,   0,   0,   0,   0,
     :   -2,   0,   2,   2,   2,   0,   0,   0,   0,
     :    1,   1,   2,  -2,   2,   0,   0,   0,   0,
     :   -2,   0,   2,   4,   2,   0,   0,   0,   0,
     :   -1,   0,   4,   0,   2,   0,   0,   0,   0,
     :    2,   0,   2,  -2,   1,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=81,90 ) /
     :    1,   0,   0,  -1,  -1,   0,   0,   0,   0,
     :    2,   0,   2,   2,   2,   0,   0,   0,   0,
     :    1,   0,   0,   2,   1,   0,   0,   0,   0,
     :    3,   0,   0,   0,   0,   0,   0,   0,   0,
     :    0,   0,   2,  -2,  -1,   0,   0,   0,   0,
     :    3,   0,   2,  -2,   2,   0,   0,   0,   0,
     :    0,   0,   4,  -2,   2,   0,   0,   0,   0,
     :   -1,   0,   0,   4,   0,   0,   0,   0,   0,
     :    0,   1,   2,   0,   1,   0,   0,   0,   0,
     :    0,   0,   2,  -2,   3,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=91,100 ) /
     :   -2,   0,   0,   4,   0,   0,   0,   0,   0,
     :   -1,  -1,   0,   2,   1,   0,   0,   0,   0,
     :   -2,   0,   2,   0,  -1,   0,   0,   0,   0,
     :    0,   0,   2,   0,  -1,   0,   0,   0,   0,
     :    0,  -1,   2,   0,   1,   0,   0,   0,   0,
     :    0,   1,   0,   0,   2,   0,   0,   0,   0,
     :    0,   0,   2,  -1,   2,   0,   0,   0,   0,
     :    2,   1,   0,  -2,   0,   0,   0,   0,   0,
     :    0,   0,   2,   4,   2,   0,   0,   0,   0,
     :   -1,  -1,   0,   2,  -1,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=101,110 ) /
     :   -1,   1,   0,   2,   0,   0,   0,   0,   0,
     :    1,  -1,   0,   0,   1,   0,   0,   0,   0,
     :    0,  -1,   2,  -2,   0,   0,   0,   0,   0,
     :    0,   1,   0,   0,  -2,   0,   0,   0,   0,
     :    1,  -1,   2,   2,   2,   0,   0,   0,   0,
     :    1,   0,   0,   2,  -1,   0,   0,   0,   0,
     :   -1,   1,   2,   2,   2,   0,   0,   0,   0,
     :    3,   0,   2,   0,   1,   0,   0,   0,   0,
     :    0,   1,   2,   2,   2,   0,   0,   0,   0,
     :    1,   0,   2,  -2,   0,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=111,120 ) /
     :   -1,   0,  -2,   4,  -1,   0,   0,   0,   0,
     :   -1,  -1,   2,   2,   1,   0,   0,   0,   0,
     :    0,  -1,   2,   2,   1,   0,   0,   0,   0,
     :    2,  -1,   2,   0,   2,   0,   0,   0,   0,
     :    0,   0,   0,   2,   2,   0,   0,   0,   0,
     :    1,  -1,   2,   0,   1,   0,   0,   0,   0,
     :   -1,   1,   2,   0,   2,   0,   0,   0,   0,
     :    0,   1,   0,   2,   0,   0,   0,   0,   0,
     :    0,   1,   2,  -2,   0,   0,   0,   0,   0,
     :    0,   3,   2,  -2,   2,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=121,130 ) /
     :    0,   0,   0,   1,   1,   0,   0,   0,   0,
     :   -1,   0,   2,   2,   0,   0,   0,   0,   0,
     :    2,   1,   2,   0,   2,   0,   0,   0,   0,
     :    1,   1,   0,   0,   1,   0,   0,   0,   0,
     :    2,   0,   0,   2,   0,   0,   0,   0,   0,
     :    1,   1,   2,   0,   1,   0,   0,   0,   0,
     :   -1,   0,   0,   2,   2,   0,   0,   0,   0,
     :    1,   0,  -2,   2,   0,   0,   0,   0,   0,
     :    0,  -1,   0,   2,  -1,   0,   0,   0,   0,
     :   -1,   0,   1,   0,   2,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=131,140 ) /
     :    0,   1,   0,   1,   0,   0,   0,   0,   0,
     :    1,   0,  -2,   2,  -2,   0,   0,   0,   0,
     :    0,   0,   0,   1,  -1,   0,   0,   0,   0,
     :    1,  -1,   0,   0,  -1,   0,   0,   0,   0,
     :    0,   0,   0,   4,   0,   0,   0,   0,   0,
     :    1,  -1,   0,   2,   0,   0,   0,   0,   0,
     :    1,   0,   2,   1,   2,   0,   0,   0,   0,
     :    1,   0,   2,  -1,   2,   0,   0,   0,   0,
     :   -1,   0,   0,   2,  -2,   0,   0,   0,   0,
     :    0,   0,   2,   1,   1,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=141,150 ) /
     :   -1,   0,   2,   0,  -1,   0,   0,   0,   0,
     :   -1,   0,   2,   4,   1,   0,   0,   0,   0,
     :    0,   0,   2,   2,   0,   0,   0,   0,   0,
     :    1,   1,   2,  -2,   1,   0,   0,   0,   0,
     :    0,   0,   1,   0,   1,   0,   0,   0,   0,
     :   -1,   0,   2,  -1,   1,   0,   0,   0,   0,
     :   -2,   0,   2,   2,   1,   0,   0,   0,   0,
     :    2,  -1,   0,   0,   0,   0,   0,   0,   0,
     :    4,   0,   2,   0,   2,   0,   0,   0,   0,
     :    2,   1,   2,  -2,   2,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=151,160 ) /
     :    0,   1,   2,   1,   2,   0,   0,   0,   0,
     :    1,   0,   4,  -2,   2,   0,   0,   0,   0,
     :    1,   1,   0,   0,  -1,   0,   0,   0,   0,
     :   -2,   0,   2,   4,   1,   0,   0,   0,   0,
     :    2,   0,   2,   0,   0,   0,   0,   0,   0,
     :   -1,   0,   1,   0,   0,   0,   0,   0,   0,
     :    1,   0,   0,   1,   0,   0,   0,   0,   0,
     :    0,   1,   0,   2,   1,   0,   0,   0,   0,
     :   -1,   0,   4,   0,   1,   0,   0,   0,   0,
     :   -1,   0,   0,   4,   1,   0,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=161,170 ) /
     :    2,   0,   2,   2,   1,   0,   0,   0,   0,
     :    2,   1,   0,   0,   0,   0,   0,   0,   0,
     :    0,   0,   5,  -5,   5,  -3,   0,   0,   0,
     :    0,   0,   0,   0,   0,   0,   0,   2,   0,
     :    0,   0,   1,  -1,   1,   0,   0,  -1,   0,
     :    0,   0,  -1,   1,  -1,   1,   0,   0,   0,
     :    0,   0,  -1,   1,   0,   0,   2,   0,   0,
     :    0,   0,   3,  -3,   3,   0,   0,  -1,   0,
     :    0,   0,  -8,   8,  -7,   5,   0,   0,   0,
     :    0,   0,  -1,   1,  -1,   0,   2,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=171,180 ) /
     :    0,   0,  -2,   2,  -2,   2,   0,   0,   0,
     :    0,   0,  -6,   6,  -6,   4,   0,   0,   0,
     :    0,   0,  -2,   2,  -2,   0,   8,  -3,   0,
     :    0,   0,   6,  -6,   6,   0,  -8,   3,   0,
     :    0,   0,   4,  -4,   4,  -2,   0,   0,   0,
     :    0,   0,  -3,   3,  -3,   2,   0,   0,   0,
     :    0,   0,   4,  -4,   3,   0,  -8,   3,   0,
     :    0,   0,  -4,   4,  -5,   0,   8,  -3,   0,
     :    0,   0,   0,   0,   0,   2,   0,   0,   0,
     :    0,   0,  -4,   4,  -4,   3,   0,   0,   0 /
      DATA ( ( NA(I,J), I=1,9 ), J=181,190 ) /
     :    0,   1,  -1,   1,  -1,   0,   0,   1,   0,
     :    0,   0,   0,   0,   0,   0,   0,   1,   0,
     :    0,   0,   1,  -1,   1,   1,   0,   0,   0,
     :    0,   0,   2,  -2,   2,   0,  -2,   0,   0,
     :    0,  -1,  -7,   7,  -7,   5,   0,   0,   0,
     :   -2,   0,   2,   0,   2,   0,   0,  -2,   0,
     :   -2,   0,   2,   0,   1,   0,   0,  -3,   0,
     :    0,   0,   2,  -2,   2,   0,   0,  -2,   0,
     :    0,   0,   1,  -1,   1,   0,   0,   1,   0,
     :    0,   0,   0,   0,   0,   0,   0,   0,   2 /
      DATA ( ( NA(I,J), I=1,9 ), J=191,NTERMS ) /
     :    0,   0,   0,   0,   0,   0,   0,   0,   1,
     :    2,   0,  -2,   0,  -2,   0,   0,   3,   0,
     :    0,   0,   1,  -1,   1,   0,   0,  -2,   0,
     :    0,   0,  -7,   7,  -7,   5,   0,   0,   0 /

*  Nutation series: longitude
      DATA ( ( PSI(I,J), I=1,4 ), J=1,10 ) /
     :  3341.5D0, 17206241.8D0,  3.1D0, 17409.5D0,
     : -1716.8D0, -1317185.3D0,  1.4D0,  -156.8D0,
     :   285.7D0,  -227667.0D0,  0.3D0,   -23.5D0,
     :   -68.6D0,  -207448.0D0,  0.0D0,   -21.4D0,
     :   950.3D0,   147607.9D0, -2.3D0,  -355.0D0,
     :   -66.7D0,   -51689.1D0,  0.2D0,   122.6D0,
     :  -108.6D0,    71117.6D0,  0.0D0,     7.0D0,
     :    35.6D0,   -38740.2D0,  0.1D0,   -36.2D0,
     :    85.4D0,   -30127.6D0,  0.0D0,    -3.1D0,
     :     9.0D0,    21583.0D0,  0.1D0,   -50.3D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=11,20 ) /
     :    22.1D0,    12822.8D0,  0.0D0,    13.3D0,
     :     3.4D0,    12350.8D0,  0.0D0,     1.3D0,
     :   -21.1D0,    15699.4D0,  0.0D0,     1.6D0,
     :     4.2D0,     6313.8D0,  0.0D0,     6.2D0,
     :   -22.8D0,     5796.9D0,  0.0D0,     6.1D0,
     :    15.7D0,    -5961.1D0,  0.0D0,    -0.6D0,
     :    13.1D0,    -5159.1D0,  0.0D0,    -4.6D0,
     :     1.8D0,     4592.7D0,  0.0D0,     4.5D0,
     :   -17.5D0,     6336.0D0,  0.0D0,     0.7D0,
     :    16.3D0,    -3851.1D0,  0.0D0,    -0.4D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=21,30 ) /
     :    -2.8D0,     4771.7D0,  0.0D0,     0.5D0,
     :    13.8D0,    -3099.3D0,  0.0D0,    -0.3D0,
     :     0.2D0,     2860.3D0,  0.0D0,     0.3D0,
     :     1.4D0,     2045.3D0,  0.0D0,     2.0D0,
     :    -8.6D0,     2922.6D0,  0.0D0,     0.3D0,
     :    -7.7D0,     2587.9D0,  0.0D0,     0.2D0,
     :     8.8D0,    -1408.1D0,  0.0D0,     3.7D0,
     :     1.4D0,     1517.5D0,  0.0D0,     1.5D0,
     :    -1.9D0,    -1579.7D0,  0.0D0,     7.7D0,
     :     1.3D0,    -2178.6D0,  0.0D0,    -0.2D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=31,40 ) /
     :    -4.8D0,     1286.8D0,  0.0D0,     1.3D0,
     :     6.3D0,     1267.2D0,  0.0D0,    -4.0D0,
     :    -1.0D0,     1669.3D0,  0.0D0,    -8.3D0,
     :     2.4D0,    -1020.0D0,  0.0D0,    -0.9D0,
     :     4.5D0,     -766.9D0,  0.0D0,     0.0D0,
     :    -1.1D0,      756.5D0,  0.0D0,    -1.7D0,
     :    -1.4D0,    -1097.3D0,  0.0D0,    -0.5D0,
     :     2.6D0,     -663.0D0,  0.0D0,    -0.6D0,
     :     0.8D0,     -714.1D0,  0.0D0,     1.6D0,
     :     0.4D0,     -629.9D0,  0.0D0,    -0.6D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=41,50 ) /
     :     0.3D0,      580.4D0,  0.0D0,     0.6D0,
     :    -1.6D0,      577.3D0,  0.0D0,     0.5D0,
     :    -0.9D0,      644.4D0,  0.0D0,     0.0D0,
     :     2.2D0,     -534.0D0,  0.0D0,    -0.5D0,
     :    -2.5D0,      493.3D0,  0.0D0,     0.5D0,
     :    -0.1D0,     -477.3D0,  0.0D0,    -2.4D0,
     :    -0.9D0,      735.0D0,  0.0D0,    -1.7D0,
     :     0.7D0,      406.2D0,  0.0D0,     0.4D0,
     :    -2.8D0,      656.9D0,  0.0D0,     0.0D0,
     :     0.6D0,      358.0D0,  0.0D0,     2.0D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=51,60 ) /
     :    -0.7D0,      472.5D0,  0.0D0,    -1.1D0,
     :    -0.1D0,     -300.5D0,  0.0D0,     0.0D0,
     :    -1.2D0,      435.1D0,  0.0D0,    -1.0D0,
     :     1.8D0,     -289.4D0,  0.0D0,     0.0D0,
     :     0.6D0,     -422.6D0,  0.0D0,     0.0D0,
     :     0.8D0,     -287.6D0,  0.0D0,     0.6D0,
     :   -38.6D0,     -392.3D0,  0.0D0,     0.0D0,
     :     0.7D0,     -281.8D0,  0.0D0,     0.6D0,
     :     0.6D0,     -405.7D0,  0.0D0,     0.0D0,
     :    -1.2D0,      229.0D0,  0.0D0,     0.2D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=61,70 ) /
     :     1.1D0,     -264.3D0,  0.0D0,     0.5D0,
     :    -0.7D0,      247.9D0,  0.0D0,    -0.5D0,
     :    -0.2D0,      218.0D0,  0.0D0,     0.2D0,
     :     0.6D0,     -339.0D0,  0.0D0,     0.8D0,
     :    -0.7D0,      198.7D0,  0.0D0,     0.2D0,
     :    -1.5D0,      334.0D0,  0.0D0,     0.0D0,
     :     0.1D0,      334.0D0,  0.0D0,     0.0D0,
     :    -0.1D0,     -198.1D0,  0.0D0,     0.0D0,
     :  -106.6D0,        0.0D0,  0.0D0,     0.0D0,
     :    -0.5D0,      165.8D0,  0.0D0,     0.0D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=71,80 ) /
     :     0.0D0,      134.8D0,  0.0D0,     0.0D0,
     :     0.9D0,     -151.6D0,  0.0D0,     0.0D0,
     :     0.0D0,     -129.7D0,  0.0D0,     0.0D0,
     :     0.8D0,     -132.8D0,  0.0D0,    -0.1D0,
     :     0.5D0,     -140.7D0,  0.0D0,     0.0D0,
     :    -0.1D0,      138.4D0,  0.0D0,     0.0D0,
     :     0.0D0,      129.0D0,  0.0D0,    -0.3D0,
     :     0.5D0,     -121.2D0,  0.0D0,     0.0D0,
     :    -0.3D0,      114.5D0,  0.0D0,     0.0D0,
     :    -0.1D0,      101.8D0,  0.0D0,     0.0D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=81,90 ) /
     :    -3.6D0,     -101.9D0,  0.0D0,     0.0D0,
     :     0.8D0,     -109.4D0,  0.0D0,     0.0D0,
     :     0.2D0,      -97.0D0,  0.0D0,     0.0D0,
     :    -0.7D0,      157.3D0,  0.0D0,     0.0D0,
     :     0.2D0,      -83.3D0,  0.0D0,     0.0D0,
     :    -0.3D0,       93.3D0,  0.0D0,     0.0D0,
     :    -0.1D0,       92.1D0,  0.0D0,     0.0D0,
     :    -0.5D0,      133.6D0,  0.0D0,     0.0D0,
     :    -0.1D0,       81.5D0,  0.0D0,     0.0D0,
     :     0.0D0,      123.9D0,  0.0D0,     0.0D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=91,100 ) /
     :    -0.3D0,      128.1D0,  0.0D0,     0.0D0,
     :     0.1D0,       74.1D0,  0.0D0,    -0.3D0,
     :    -0.2D0,      -70.3D0,  0.0D0,     0.0D0,
     :    -0.4D0,       66.6D0,  0.0D0,     0.0D0,
     :     0.1D0,      -66.7D0,  0.0D0,     0.0D0,
     :    -0.7D0,       69.3D0,  0.0D0,    -0.3D0,
     :     0.0D0,      -70.4D0,  0.0D0,     0.0D0,
     :    -0.1D0,      101.5D0,  0.0D0,     0.0D0,
     :     0.5D0,      -69.1D0,  0.0D0,     0.0D0,
     :    -0.2D0,       58.5D0,  0.0D0,     0.2D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=101,110 ) /
     :     0.1D0,      -94.9D0,  0.0D0,     0.2D0,
     :     0.0D0,       52.9D0,  0.0D0,    -0.2D0,
     :     0.1D0,       86.7D0,  0.0D0,    -0.2D0,
     :    -0.1D0,      -59.2D0,  0.0D0,     0.2D0,
     :     0.3D0,      -58.8D0,  0.0D0,     0.1D0,
     :    -0.3D0,       49.0D0,  0.0D0,     0.0D0,
     :    -0.2D0,       56.9D0,  0.0D0,    -0.1D0,
     :     0.3D0,      -50.2D0,  0.0D0,     0.0D0,
     :    -0.2D0,       53.4D0,  0.0D0,    -0.1D0,
     :     0.1D0,      -76.5D0,  0.0D0,     0.0D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=111,120 ) /
     :    -0.2D0,       45.3D0,  0.0D0,     0.0D0,
     :     0.1D0,      -46.8D0,  0.0D0,     0.0D0,
     :     0.2D0,      -44.6D0,  0.0D0,     0.0D0,
     :     0.2D0,      -48.7D0,  0.0D0,     0.0D0,
     :     0.1D0,      -46.8D0,  0.0D0,     0.0D0,
     :     0.1D0,      -42.0D0,  0.0D0,     0.0D0,
     :     0.0D0,       46.4D0,  0.0D0,    -0.1D0,
     :     0.2D0,      -67.3D0,  0.0D0,     0.1D0,
     :     0.0D0,      -65.8D0,  0.0D0,     0.2D0,
     :    -0.1D0,      -43.9D0,  0.0D0,     0.3D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=121,130 ) /
     :     0.0D0,      -38.9D0,  0.0D0,     0.0D0,
     :    -0.3D0,       63.9D0,  0.0D0,     0.0D0,
     :    -0.2D0,       41.2D0,  0.0D0,     0.0D0,
     :     0.0D0,      -36.1D0,  0.0D0,     0.2D0,
     :    -0.3D0,       58.5D0,  0.0D0,     0.0D0,
     :    -0.1D0,       36.1D0,  0.0D0,     0.0D0,
     :     0.0D0,      -39.7D0,  0.0D0,     0.0D0,
     :     0.1D0,      -57.7D0,  0.0D0,     0.0D0,
     :    -0.2D0,       33.4D0,  0.0D0,     0.0D0,
     :    36.4D0,        0.0D0,  0.0D0,     0.0D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=131,140 ) /
     :    -0.1D0,       55.7D0,  0.0D0,    -0.1D0,
     :     0.1D0,      -35.4D0,  0.0D0,     0.0D0,
     :     0.1D0,      -31.0D0,  0.0D0,     0.0D0,
     :    -0.1D0,       30.1D0,  0.0D0,     0.0D0,
     :    -0.3D0,       49.2D0,  0.0D0,     0.0D0,
     :    -0.2D0,       49.1D0,  0.0D0,     0.0D0,
     :    -0.1D0,       33.6D0,  0.0D0,     0.0D0,
     :     0.1D0,      -33.5D0,  0.0D0,     0.0D0,
     :     0.1D0,      -31.0D0,  0.0D0,     0.0D0,
     :    -0.1D0,       28.0D0,  0.0D0,     0.0D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=141,150 ) /
     :     0.1D0,      -25.2D0,  0.0D0,     0.0D0,
     :     0.1D0,      -26.2D0,  0.0D0,     0.0D0,
     :    -0.2D0,       41.5D0,  0.0D0,     0.0D0,
     :     0.0D0,       24.5D0,  0.0D0,     0.1D0,
     :   -16.2D0,        0.0D0,  0.0D0,     0.0D0,
     :     0.0D0,      -22.3D0,  0.0D0,     0.0D0,
     :     0.0D0,       23.1D0,  0.0D0,     0.0D0,
     :    -0.1D0,       37.5D0,  0.0D0,     0.0D0,
     :     0.2D0,      -25.7D0,  0.0D0,     0.0D0,
     :     0.0D0,       25.2D0,  0.0D0,     0.0D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=151,160 ) /
     :     0.1D0,      -24.5D0,  0.0D0,     0.0D0,
     :    -0.1D0,       24.3D0,  0.0D0,     0.0D0,
     :     0.1D0,      -20.7D0,  0.0D0,     0.0D0,
     :     0.1D0,      -20.8D0,  0.0D0,     0.0D0,
     :    -0.2D0,       33.4D0,  0.0D0,     0.0D0,
     :    32.9D0,        0.0D0,  0.0D0,     0.0D0,
     :     0.1D0,      -32.6D0,  0.0D0,     0.0D0,
     :     0.0D0,       19.9D0,  0.0D0,     0.0D0,
     :    -0.1D0,       19.6D0,  0.0D0,     0.0D0,
     :     0.0D0,      -18.7D0,  0.0D0,     0.0D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=161,170 ) /
     :     0.1D0,      -19.0D0,  0.0D0,     0.0D0,
     :     0.1D0,      -28.6D0,  0.0D0,     0.0D0,
     :     4.0D0,      178.8D0,-11.8D0,     0.3D0,
     :    39.8D0,     -107.3D0, -5.6D0,    -1.0D0,
     :     9.9D0,      164.0D0, -4.1D0,     0.1D0,
     :    -4.8D0,     -135.3D0, -3.4D0,    -0.1D0,
     :    50.5D0,       75.0D0,  1.4D0,    -1.2D0,
     :    -1.1D0,      -53.5D0,  1.3D0,     0.0D0,
     :   -45.0D0,       -2.4D0, -0.4D0,     6.6D0,
     :   -11.5D0,      -61.0D0, -0.9D0,     0.4D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=171,180 ) /
     :     4.4D0,      -68.4D0, -3.4D0,     0.0D0,
     :     7.7D0,      -47.1D0, -4.7D0,    -1.0D0,
     :   -42.9D0,      -12.6D0, -1.2D0,     4.2D0,
     :   -42.8D0,       12.7D0, -1.2D0,    -4.2D0,
     :    -7.6D0,      -44.1D0,  2.1D0,    -0.5D0,
     :   -64.1D0,        1.7D0,  0.2D0,     4.5D0,
     :    36.4D0,      -10.4D0,  1.0D0,     3.5D0,
     :    35.6D0,       10.2D0,  1.0D0,    -3.5D0,
     :    -1.7D0,       39.5D0,  2.0D0,     0.0D0,
     :    50.9D0,       -8.2D0, -0.8D0,    -5.0D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=181,190 ) /
     :     0.0D0,       52.3D0,  1.2D0,     0.0D0,
     :   -42.9D0,      -17.8D0,  0.4D0,     0.0D0,
     :     2.6D0,       34.3D0,  0.8D0,     0.0D0,
     :    -0.8D0,      -48.6D0,  2.4D0,    -0.1D0,
     :    -4.9D0,       30.5D0,  3.7D0,     0.7D0,
     :     0.0D0,      -43.6D0,  2.1D0,     0.0D0,
     :     0.0D0,      -25.4D0,  1.2D0,     0.0D0,
     :     2.0D0,       40.9D0, -2.0D0,     0.0D0,
     :    -2.1D0,       26.1D0,  0.6D0,     0.0D0,
     :    22.6D0,       -3.2D0, -0.5D0,    -0.5D0 /
      DATA ( ( PSI(I,J), I=1,4 ), J=191,NTERMS ) /
     :    -7.6D0,       24.9D0, -0.4D0,    -0.2D0,
     :    -6.2D0,       34.9D0,  1.7D0,     0.3D0,
     :     2.0D0,       17.4D0, -0.4D0,     0.1D0,
     :    -3.9D0,       20.5D0,  2.4D0,     0.6D0 /

*  Nutation series: obliquity
      DATA ( ( EPS(I,J), I=1,4 ), J=1,10 ) /
     : 9205365.8D0, -1506.2D0,  885.7D0, -0.2D0,
     :  573095.9D0,  -570.2D0, -305.0D0, -0.3D0,
     :   97845.5D0,   147.8D0,  -48.8D0, -0.2D0,
     :  -89753.6D0,    28.0D0,   46.9D0,  0.0D0,
     :    7406.7D0,  -327.1D0,  -18.2D0,  0.8D0,
     :   22442.3D0,   -22.3D0,  -67.6D0,  0.0D0,
     :    -683.6D0,    46.8D0,    0.0D0,  0.0D0,
     :   20070.7D0,    36.0D0,    1.6D0,  0.0D0,
     :   12893.8D0,    39.5D0,   -6.2D0,  0.0D0,
     :   -9593.2D0,    14.4D0,   30.2D0, -0.1D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=11,20 ) /
     :   -6899.5D0,     4.8D0,   -0.6D0,  0.0D0,
     :   -5332.5D0,    -0.1D0,    2.7D0,  0.0D0,
     :    -125.2D0,    10.5D0,    0.0D0,  0.0D0,
     :   -3323.4D0,    -0.9D0,   -0.3D0,  0.0D0,
     :    3142.3D0,     8.9D0,    0.3D0,  0.0D0,
     :    2552.5D0,     7.3D0,   -1.2D0,  0.0D0,
     :    2634.4D0,     8.8D0,    0.2D0,  0.0D0,
     :   -2424.4D0,     1.6D0,   -0.4D0,  0.0D0,
     :    -123.3D0,     3.9D0,    0.0D0,  0.0D0,
     :    1642.4D0,     7.3D0,   -0.8D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=21,30 ) /
     :      47.9D0,     3.2D0,    0.0D0,  0.0D0,
     :    1321.2D0,     6.2D0,   -0.6D0,  0.0D0,
     :   -1234.1D0,    -0.3D0,    0.6D0,  0.0D0,
     :   -1076.5D0,    -0.3D0,    0.0D0,  0.0D0,
     :     -61.6D0,     1.8D0,    0.0D0,  0.0D0,
     :     -55.4D0,     1.6D0,    0.0D0,  0.0D0,
     :     856.9D0,    -4.9D0,   -2.1D0,  0.0D0,
     :    -800.7D0,    -0.1D0,    0.0D0,  0.0D0,
     :     685.1D0,    -0.6D0,   -3.8D0,  0.0D0,
     :     -16.9D0,    -1.5D0,    0.0D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=31,40 ) /
     :     695.7D0,     1.8D0,    0.0D0,  0.0D0,
     :     642.2D0,    -2.6D0,   -1.6D0,  0.0D0,
     :      13.3D0,     1.1D0,   -0.1D0,  0.0D0,
     :     521.9D0,     1.6D0,    0.0D0,  0.0D0,
     :     325.8D0,     2.0D0,   -0.1D0,  0.0D0,
     :    -325.1D0,    -0.5D0,    0.9D0,  0.0D0,
     :      10.1D0,     0.3D0,    0.0D0,  0.0D0,
     :     334.5D0,     1.6D0,    0.0D0,  0.0D0,
     :     307.1D0,     0.4D0,   -0.9D0,  0.0D0,
     :     327.2D0,     0.5D0,    0.0D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=41,50 ) /
     :    -304.6D0,    -0.1D0,    0.0D0,  0.0D0,
     :     304.0D0,     0.6D0,    0.0D0,  0.0D0,
     :    -276.8D0,    -0.5D0,    0.1D0,  0.0D0,
     :     268.9D0,     1.3D0,    0.0D0,  0.0D0,
     :     271.8D0,     1.1D0,    0.0D0,  0.0D0,
     :     271.5D0,    -0.4D0,   -0.8D0,  0.0D0,
     :      -5.2D0,     0.5D0,    0.0D0,  0.0D0,
     :    -220.5D0,     0.1D0,    0.0D0,  0.0D0,
     :     -20.1D0,     0.3D0,    0.0D0,  0.0D0,
     :    -191.0D0,     0.1D0,    0.5D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=51,60 ) /
     :      -4.1D0,     0.3D0,    0.0D0,  0.0D0,
     :     130.6D0,    -0.1D0,    0.0D0,  0.0D0,
     :       3.0D0,     0.3D0,    0.0D0,  0.0D0,
     :     122.9D0,     0.8D0,    0.0D0,  0.0D0,
     :       3.7D0,    -0.3D0,    0.0D0,  0.0D0,
     :     123.1D0,     0.4D0,   -0.3D0,  0.0D0,
     :     -52.7D0,    15.3D0,    0.0D0,  0.0D0,
     :     120.7D0,     0.3D0,   -0.3D0,  0.0D0,
     :       4.0D0,    -0.3D0,    0.0D0,  0.0D0,
     :     126.5D0,     0.5D0,    0.0D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=61,70 ) /
     :     112.7D0,     0.5D0,   -0.3D0,  0.0D0,
     :    -106.1D0,    -0.3D0,    0.3D0,  0.0D0,
     :    -112.9D0,    -0.2D0,    0.0D0,  0.0D0,
     :       3.6D0,    -0.2D0,    0.0D0,  0.0D0,
     :     107.4D0,     0.3D0,    0.0D0,  0.0D0,
     :     -10.9D0,     0.2D0,    0.0D0,  0.0D0,
     :      -0.9D0,     0.0D0,    0.0D0,  0.0D0,
     :      85.4D0,     0.0D0,    0.0D0,  0.0D0,
     :       0.0D0,   -88.8D0,    0.0D0,  0.0D0,
     :     -71.0D0,    -0.2D0,    0.0D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=71,80 ) /
     :     -70.3D0,     0.0D0,    0.0D0,  0.0D0,
     :      64.5D0,     0.4D0,    0.0D0,  0.0D0,
     :      69.8D0,     0.0D0,    0.0D0,  0.0D0,
     :      66.1D0,     0.4D0,    0.0D0,  0.0D0,
     :     -61.0D0,    -0.2D0,    0.0D0,  0.0D0,
     :     -59.5D0,    -0.1D0,    0.0D0,  0.0D0,
     :     -55.6D0,     0.0D0,    0.2D0,  0.0D0,
     :      51.7D0,     0.2D0,    0.0D0,  0.0D0,
     :     -49.0D0,    -0.1D0,    0.0D0,  0.0D0,
     :     -52.7D0,    -0.1D0,    0.0D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=81,90 ) /
     :     -49.6D0,     1.4D0,    0.0D0,  0.0D0,
     :      46.3D0,     0.4D0,    0.0D0,  0.0D0,
     :      49.6D0,     0.1D0,    0.0D0,  0.0D0,
     :      -5.1D0,     0.1D0,    0.0D0,  0.0D0,
     :     -44.0D0,    -0.1D0,    0.0D0,  0.0D0,
     :     -39.9D0,    -0.1D0,    0.0D0,  0.0D0,
     :     -39.5D0,    -0.1D0,    0.0D0,  0.0D0,
     :      -3.9D0,     0.1D0,    0.0D0,  0.0D0,
     :     -42.1D0,    -0.1D0,    0.0D0,  0.0D0,
     :     -17.2D0,     0.1D0,    0.0D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=91,100 ) /
     :      -2.3D0,     0.1D0,    0.0D0,  0.0D0,
     :     -39.2D0,     0.0D0,    0.0D0,  0.0D0,
     :     -38.4D0,     0.1D0,    0.0D0,  0.0D0,
     :      36.8D0,     0.2D0,    0.0D0,  0.0D0,
     :      34.6D0,     0.1D0,    0.0D0,  0.0D0,
     :     -32.7D0,     0.3D0,    0.0D0,  0.0D0,
     :      30.4D0,     0.0D0,    0.0D0,  0.0D0,
     :       0.4D0,     0.1D0,    0.0D0,  0.0D0,
     :      29.3D0,     0.2D0,    0.0D0,  0.0D0,
     :      31.6D0,     0.1D0,    0.0D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=101,110 ) /
     :       0.8D0,    -0.1D0,    0.0D0,  0.0D0,
     :     -27.9D0,     0.0D0,    0.0D0,  0.0D0,
     :       2.9D0,     0.0D0,    0.0D0,  0.0D0,
     :     -25.3D0,     0.0D0,    0.0D0,  0.0D0,
     :      25.0D0,     0.1D0,    0.0D0,  0.0D0,
     :      27.5D0,     0.1D0,    0.0D0,  0.0D0,
     :     -24.4D0,    -0.1D0,    0.0D0,  0.0D0,
     :      24.9D0,     0.2D0,    0.0D0,  0.0D0,
     :     -22.8D0,    -0.1D0,    0.0D0,  0.0D0,
     :       0.9D0,    -0.1D0,    0.0D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=111,120 ) /
     :      24.4D0,     0.1D0,    0.0D0,  0.0D0,
     :      23.9D0,     0.1D0,    0.0D0,  0.0D0,
     :      22.5D0,     0.1D0,    0.0D0,  0.0D0,
     :      20.8D0,     0.1D0,    0.0D0,  0.0D0,
     :      20.1D0,     0.0D0,    0.0D0,  0.0D0,
     :      21.5D0,     0.1D0,    0.0D0,  0.0D0,
     :     -20.0D0,     0.0D0,    0.0D0,  0.0D0,
     :       1.4D0,     0.0D0,    0.0D0,  0.0D0,
     :      -0.2D0,    -0.1D0,    0.0D0,  0.0D0,
     :      19.0D0,     0.0D0,   -0.1D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=121,130 ) /
     :      20.5D0,     0.0D0,    0.0D0,  0.0D0,
     :      -2.0D0,     0.0D0,    0.0D0,  0.0D0,
     :     -17.6D0,    -0.1D0,    0.0D0,  0.0D0,
     :      19.0D0,     0.0D0,    0.0D0,  0.0D0,
     :      -2.4D0,     0.0D0,    0.0D0,  0.0D0,
     :     -18.4D0,    -0.1D0,    0.0D0,  0.0D0,
     :      17.1D0,     0.0D0,    0.0D0,  0.0D0,
     :       0.4D0,     0.0D0,    0.0D0,  0.0D0,
     :      18.4D0,     0.1D0,    0.0D0,  0.0D0,
     :       0.0D0,    17.4D0,    0.0D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=131,140 ) /
     :      -0.6D0,     0.0D0,    0.0D0,  0.0D0,
     :     -15.4D0,     0.0D0,    0.0D0,  0.0D0,
     :     -16.8D0,    -0.1D0,    0.0D0,  0.0D0,
     :      16.3D0,     0.0D0,    0.0D0,  0.0D0,
     :      -2.0D0,     0.0D0,    0.0D0,  0.0D0,
     :      -1.5D0,     0.0D0,    0.0D0,  0.0D0,
     :     -14.3D0,    -0.1D0,    0.0D0,  0.0D0,
     :      14.4D0,     0.0D0,    0.0D0,  0.0D0,
     :     -13.4D0,     0.0D0,    0.0D0,  0.0D0,
     :     -14.3D0,    -0.1D0,    0.0D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=141,150 ) /
     :     -13.7D0,     0.0D0,    0.0D0,  0.0D0,
     :      13.1D0,     0.1D0,    0.0D0,  0.0D0,
     :      -1.7D0,     0.0D0,    0.0D0,  0.0D0,
     :     -12.8D0,     0.0D0,    0.0D0,  0.0D0,
     :       0.0D0,   -14.4D0,    0.0D0,  0.0D0,
     :      12.4D0,     0.0D0,    0.0D0,  0.0D0,
     :     -12.0D0,     0.0D0,    0.0D0,  0.0D0,
     :      -0.8D0,     0.0D0,    0.0D0,  0.0D0,
     :      10.9D0,     0.1D0,    0.0D0,  0.0D0,
     :     -10.8D0,     0.0D0,    0.0D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=151,160 ) /
     :      10.5D0,     0.0D0,    0.0D0,  0.0D0,
     :     -10.4D0,     0.0D0,    0.0D0,  0.0D0,
     :     -11.2D0,     0.0D0,    0.0D0,  0.0D0,
     :      10.5D0,     0.1D0,    0.0D0,  0.0D0,
     :      -1.4D0,     0.0D0,    0.0D0,  0.0D0,
     :       0.0D0,     0.1D0,    0.0D0,  0.0D0,
     :       0.7D0,     0.0D0,    0.0D0,  0.0D0,
     :     -10.3D0,     0.0D0,    0.0D0,  0.0D0,
     :     -10.0D0,     0.0D0,    0.0D0,  0.0D0,
     :       9.6D0,     0.0D0,    0.0D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=161,170 ) /
     :       9.4D0,     0.1D0,    0.0D0,  0.0D0,
     :       0.6D0,     0.0D0,    0.0D0,  0.0D0,
     :     -87.7D0,     4.4D0,   -0.4D0, -6.3D0,
     :      46.3D0,    22.4D0,    0.5D0, -2.4D0,
     :      15.6D0,    -3.4D0,    0.1D0,  0.4D0,
     :       5.2D0,     5.8D0,    0.2D0, -0.1D0,
     :     -30.1D0,    26.9D0,    0.7D0,  0.0D0,
     :      23.2D0,    -0.5D0,    0.0D0,  0.6D0,
     :       1.0D0,    23.2D0,    3.4D0,  0.0D0,
     :     -12.2D0,    -4.3D0,    0.0D0,  0.0D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=171,180 ) /
     :      -2.1D0,    -3.7D0,   -0.2D0,  0.1D0,
     :     -18.6D0,    -3.8D0,   -0.4D0,  1.8D0,
     :       5.5D0,   -18.7D0,   -1.8D0, -0.5D0,
     :      -5.5D0,   -18.7D0,    1.8D0, -0.5D0,
     :      18.4D0,    -3.6D0,    0.3D0,  0.9D0,
     :      -0.6D0,     1.3D0,    0.0D0,  0.0D0,
     :      -5.6D0,   -19.5D0,    1.9D0,  0.0D0,
     :       5.5D0,   -19.1D0,   -1.9D0,  0.0D0,
     :     -17.3D0,    -0.8D0,    0.0D0,  0.9D0,
     :      -3.2D0,    -8.3D0,   -0.8D0,  0.3D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=181,190 ) /
     :      -0.1D0,     0.0D0,    0.0D0,  0.0D0,
     :      -5.4D0,     7.8D0,   -0.3D0,  0.0D0,
     :     -14.8D0,     1.4D0,    0.0D0,  0.3D0,
     :      -3.8D0,     0.4D0,    0.0D0, -0.2D0,
     :      12.6D0,     3.2D0,    0.5D0, -1.5D0,
     :       0.1D0,     0.0D0,    0.0D0,  0.0D0,
     :     -13.6D0,     2.4D0,   -0.1D0,  0.0D0,
     :       0.9D0,     1.2D0,    0.0D0,  0.0D0,
     :     -11.9D0,    -0.5D0,    0.0D0,  0.3D0,
     :       0.4D0,    12.0D0,    0.3D0, -0.2D0 /
      DATA ( ( EPS(I,J), I=1,4 ), J=191,NTERMS ) /
     :       8.3D0,     6.1D0,   -0.1D0,  0.1D0,
     :       0.0D0,     0.0D0,    0.0D0,  0.0D0,
     :       0.4D0,   -10.8D0,    0.3D0,  0.0D0,
     :       9.6D0,     2.2D0,    0.3D0, -1.2D0 /



*  Interval between fundamental epoch J2000.0 and given epoch (JC).
      T = (DATE-DJM0)/DJC

*  Mean anomaly of the Moon.
      EL  = 134.96340251D0*DD2R+
     :      MOD(T*(1717915923.2178D0+
     :          T*(        31.8792D0+
     :          T*(         0.051635D0+
     :          T*(       - 0.00024470D0)))),TURNAS)*DAS2R

*  Mean anomaly of the Sun.
      ELP = 357.52910918D0*DD2R+
     :      MOD(T*( 129596581.0481D0+
     :          T*(       - 0.5532D0+
     :          T*(         0.000136D0+
     :          T*(       - 0.00001149D0)))),TURNAS)*DAS2R

*  Mean argument of the latitude of the Moon.
      F   =  93.27209062D0*DD2R+
     :      MOD(T*(1739527262.8478D0+
     :          T*(      - 12.7512D0+
     :          T*(      -  0.001037D0+
     :          T*(         0.00000417D0)))),TURNAS)*DAS2R

*  Mean elongation of the Moon from the Sun.
      D   = 297.85019547D0*DD2R+
     :      MOD(T*(1602961601.2090D0+
     :          T*(       - 6.3706D0+
     :          T*(         0.006539D0+
     :          T*(       - 0.00003169D0)))),TURNAS)*DAS2R

*  Mean longitude of the ascending node of the Moon.
      OM  = 125.04455501D0*DD2R+
     :      MOD(T*( - 6962890.5431D0+
     :          T*(         7.4722D0+
     :          T*(         0.007702D0+
     :          T*(       - 0.00005939D0)))),TURNAS)*DAS2R

*  Mean longitude of Venus.
      VE    = 181.97980085D0*DD2R+MOD(210664136.433548D0*T,TURNAS)*DAS2R

*  Mean longitude of Mars.
      MA    = 355.43299958D0*DD2R+MOD( 68905077.493988D0*T,TURNAS)*DAS2R

*  Mean longitude of Jupiter.
      JU    =  34.35151874D0*DD2R+MOD( 10925660.377991D0*T,TURNAS)*DAS2R

*  Mean longitude of Saturn.
      SA    =  50.07744430D0*DD2R+MOD(  4399609.855732D0*T,TURNAS)*DAS2R

*  Geodesic nutation (Fukushima 1991) in microarcsec.
      DP = -153.1D0*SIN(ELP)-1.9D0*SIN(2D0*ELP)
      DE = 0D0

*  Shirai & Fukushima (2001) nutation series.
      DO J=NTERMS,1,-1
         THETA = DBLE(NA(1,J))*EL+
     :           DBLE(NA(2,J))*ELP+
     :           DBLE(NA(3,J))*F+
     :           DBLE(NA(4,J))*D+
     :           DBLE(NA(5,J))*OM+
     :           DBLE(NA(6,J))*VE+
     :           DBLE(NA(7,J))*MA+
     :           DBLE(NA(8,J))*JU+
     :           DBLE(NA(9,J))*SA
         C = COS(THETA)
         S = SIN(THETA)
         DP = DP+(PSI(1,J)+PSI(3,J)*T)*C+(PSI(2,J)+PSI(4,J)*T)*S
         DE = DE+(EPS(1,J)+EPS(3,J)*T)*C+(EPS(2,J)+EPS(4,J)*T)*S
      END DO

*  Change of units, and addition of the precession correction.
      DPSI = (DP*1D-6-0.042888D0-0.29856D0*T)*DAS2R
      DEPS = (DE*1D-6-0.005171D0-0.02408D0*T)*DAS2R

*  Mean obliquity of date (Simon et al. 1994).
      EPS0 = (84381.412D0+
     :         (-46.80927D0+
     :          (-0.000152D0+
     :           (0.0019989D0+
     :          (-0.00000051D0+
     :          (-0.000000025D0)*T)*T)*T)*T)*T)*DAS2R

      END
      SUBROUTINE sla_NUT (DATE, RMATN)
*+
*     - - - -
*      N U T
*     - - - -
*
*  Form the matrix of nutation for a given date - Shirai & Fukushima
*  2001 theory (double precision)
*
*  Reference:
*     Shirai, T. & Fukushima, T., Astron.J. 121, 3270-3283 (2001).
*
*  Given:
*     DATE    d          TDB (loosely ET) as Modified Julian Date
*                                           (=JD-2400000.5)
*  Returned:
*     RMATN   d(3,3)     nutation matrix
*
*  Notes:
*
*  1  The matrix is in the sense  v(true) = rmatn * v(mean) .
*     where v(true) is the star vector relative to the true equator and
*     equinox of date and v(mean) is the star vector relative to the
*     mean equator and equinox of date.
*
*  2  The matrix represents forced nutation (but not free core
*     nutation) plus corrections to the IAU~1976 precession model.
*
*  3  Earth attitude predictions made by combining the present nutation
*     matrix with IAU~1976 precession are accurate to 1~mas (with
*     respect to the ICRS) for a few decades around 2000.
*
*  4  The distinction between the required TDB and TT is always
*     negligible.  Moreover, for all but the most critical applications
*     UTC is adequate.
*
*  Called:   sla_NUTC, sla_DEULER
*
*  Last revision:   1 December 2005
*
*  Copyright P.T.Wallace.  All rights reserved.
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION DATE,RMATN(3,3)

      DOUBLE PRECISION DPSI,DEPS,EPS0



*  Nutation components and mean obliquity
      CALL sla_NUTC(DATE,DPSI,DEPS,EPS0)

*  Rotation matrix
      CALL sla_DEULER('XZX',EPS0,-DPSI,-(EPS0+DEPS),RMATN)

      END
      SUBROUTINE sla_PREBN (BEP0, BEP1, RMATP)
*+
*     - - - - - -
*      P R E B N
*     - - - - - -
*
*  Generate the matrix of precession between two epochs,
*  using the old, pre-IAU1976, Bessel-Newcomb model, using
*  Kinoshita's formulation (double precision)
*
*  Given:
*     BEP0    dp         beginning Besselian epoch
*     BEP1    dp         ending Besselian epoch
*
*  Returned:
*     RMATP  dp(3,3)    precession matrix
*
*  The matrix is in the sense   V(BEP1)  =  RMATP * V(BEP0)
*
*  Reference:
*     Kinoshita, H. (1975) 'Formulas for precession', SAO Special
*     Report No. 364, Smithsonian Institution Astrophysical
*     Observatory, Cambridge, Massachusetts.
*
*  Called:  sla_DEULER
*
*  P.T.Wallace   Starlink   23 August 1996
*
*  Copyright (C) 1996 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION BEP0,BEP1,RMATP(3,3)

*  Arc seconds to radians
      DOUBLE PRECISION AS2R
      PARAMETER (AS2R=0.484813681109535994D-5)

      DOUBLE PRECISION BIGT,T,TAS2R,W,ZETA,Z,THETA



*  Interval between basic epoch B1850.0 and beginning epoch in TC
      BIGT = (BEP0-1850D0)/100D0

*  Interval over which precession required, in tropical centuries
      T = (BEP1-BEP0)/100D0

*  Euler angles
      TAS2R = T*AS2R
      W = 2303.5548D0+(1.39720D0+0.000059D0*BIGT)*BIGT

      ZETA = (W+(0.30242D0-0.000269D0*BIGT+0.017996D0*T)*T)*TAS2R
      Z = (W+(1.09478D0+0.000387D0*BIGT+0.018324D0*T)*T)*TAS2R
      THETA = (2005.1125D0+(-0.85294D0-0.000365D0*BIGT)*BIGT+
     :        (-0.42647D0-0.000365D0*BIGT-0.041802D0*T)*T)*TAS2R

*  Rotation matrix
      CALL sla_DEULER('ZYZ',-ZETA,THETA,-Z,RMATP)

      END
      SUBROUTINE sla_PRECES (SYSTEM, EP0, EP1, RA, DC)
*+
*     - - - - - - -
*      P R E C E S
*     - - - - - - -
*
*  Precession - either FK4 (Bessel-Newcomb, pre IAU 1976) or
*  FK5 (Fricke, post IAU 1976) as required.
*
*  Given:
*     SYSTEM     char   precession to be applied: 'FK4' or 'FK5'
*     EP0,EP1    dp     starting and ending epoch
*     RA,DC      dp     RA,Dec, mean equator & equinox of epoch EP0
*
*  Returned:
*     RA,DC      dp     RA,Dec, mean equator & equinox of epoch EP1
*
*  Called:    sla_DRANRM, sla_PREBN, sla_PREC, sla_DCS2C,
*             sla_DMXV, sla_DCC2S
*
*  Notes:
*
*     1)  Lowercase characters in SYSTEM are acceptable.
*
*     2)  The epochs are Besselian if SYSTEM='FK4' and Julian if 'FK5'.
*         For example, to precess coordinates in the old system from
*         equinox 1900.0 to 1950.0 the call would be:
*             CALL sla_PRECES ('FK4', 1900D0, 1950D0, RA, DC)
*
*     3)  This routine will NOT correctly convert between the old and
*         the new systems - for example conversion from B1950 to J2000.
*         For these purposes see sla_FK425, sla_FK524, sla_FK45Z and
*         sla_FK54Z.
*
*     4)  If an invalid SYSTEM is supplied, values of -99D0,-99D0 will
*         be returned for both RA and DC.
*
*  P.T.Wallace   Starlink   20 April 1990
*
*  Copyright (C) 1995 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      CHARACTER SYSTEM*(*)
      DOUBLE PRECISION EP0,EP1,RA,DC

      DOUBLE PRECISION PM(3,3),V1(3),V2(3)
      CHARACTER SYSUC*3

      DOUBLE PRECISION sla_DRANRM




*  Convert to uppercase and validate SYSTEM
      SYSUC=SYSTEM
      IF (SYSUC(1:1).EQ.'f') SYSUC(1:1)='F'
      IF (SYSUC(2:2).EQ.'k') SYSUC(2:2)='K'
      IF (SYSUC.NE.'FK4'.AND.SYSUC.NE.'FK5') THEN
         RA=-99D0
         DC=-99D0
      ELSE

*     Generate appropriate precession matrix
         IF (SYSUC.EQ.'FK4') THEN
            CALL sla_PREBN(EP0,EP1,PM)
         ELSE
            CALL sla_PREC(EP0,EP1,PM)
         END IF

*     Convert RA,Dec to x,y,z
         CALL sla_DCS2C(RA,DC,V1)

*     Precess
         CALL sla_DMXV(PM,V1,V2)

*     Back to RA,Dec
         CALL sla_DCC2S(V2,RA,DC)
         RA=sla_DRANRM(RA)

      END IF

      END
      SUBROUTINE sla_PREC (EP0, EP1, RMATP)
*+
*     - - - - -
*      P R E C
*     - - - - -
*
*  Form the matrix of precession between two epochs (IAU 1976, FK5)
*  (double precision)
*
*  Given:
*     EP0    dp         beginning epoch
*     EP1    dp         ending epoch
*
*  Returned:
*     RMATP  dp(3,3)    precession matrix
*
*  Notes:
*
*     1)  The epochs are TDB (loosely ET) Julian epochs.
*
*     2)  The matrix is in the sense   V(EP1)  =  RMATP * V(EP0)
*
*     3)  Though the matrix method itself is rigorous, the precession
*         angles are expressed through canonical polynomials which are
*         valid only for a limited time span.  There are also known
*         errors in the IAU precession rate.  The absolute accuracy
*         of the present formulation is better than 0.1 arcsec from
*         1960AD to 2040AD, better than 1 arcsec from 1640AD to 2360AD,
*         and remains below 3 arcsec for the whole of the period
*         500BC to 3000AD.  The errors exceed 10 arcsec outside the
*         range 1200BC to 3900AD, exceed 100 arcsec outside 4200BC to
*         5600AD and exceed 1000 arcsec outside 6800BC to 8200AD.
*         The SLALIB routine sla_PRECL implements a more elaborate
*         model which is suitable for problems spanning several
*         thousand years.
*
*  References:
*     Lieske,J.H., 1979. Astron.Astrophys.,73,282.
*      equations (6) & (7), p283.
*     Kaplan,G.H., 1981. USNO circular no. 163, pA2.
*
*  Called:  sla_DEULER
*
*  P.T.Wallace   Starlink   23 August 1996
*
*  Copyright (C) 1996 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION EP0,EP1,RMATP(3,3)

*  Arc seconds to radians
      DOUBLE PRECISION AS2R
      PARAMETER (AS2R=0.484813681109535994D-5)

      DOUBLE PRECISION T0,T,TAS2R,W,ZETA,Z,THETA



*  Interval between basic epoch J2000.0 and beginning epoch (JC)
      T0 = (EP0-2000D0)/100D0

*  Interval over which precession required (JC)
      T = (EP1-EP0)/100D0

*  Euler angles
      TAS2R = T*AS2R
      W = 2306.2181D0+(1.39656D0-0.000139D0*T0)*T0

      ZETA = (W+((0.30188D0-0.000344D0*T0)+0.017998D0*T)*T)*TAS2R
      Z = (W+((1.09468D0+0.000066D0*T0)+0.018203D0*T)*T)*TAS2R
      THETA = ((2004.3109D0+(-0.85330D0-0.000217D0*T0)*T0)
     :        +((-0.42665D0-0.000217D0*T0)-0.041833D0*T)*T)*TAS2R

*  Rotation matrix
      CALL sla_DEULER('ZYZ',-ZETA,THETA,-Z,RMATP)

      END
      SUBROUTINE sla_PVOBS (P, H, STL, PV)
*+
*     - - - - - -
*      P V O B S
*     - - - - - -
*
*  Position and velocity of an observing station (double precision)
*
*  Given:
*     P     dp     latitude (geodetic, radians)
*     H     dp     height above reference spheroid (geodetic, metres)
*     STL   dp     local apparent sidereal time (radians)
*
*  Returned:
*     PV    dp(6)  position/velocity 6-vector (AU, AU/s, true equator
*                                              and equinox of date)
*
*  Called:  sla_GEOC
*
*  IAU 1976 constants are used.
*
*  P.T.Wallace   Starlink   14 November 1994
*
*  Copyright (C) 1995 Rutherford Appleton Laboratory
*
*  License:
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program (see SLA_CONDITIONS); if not, write to the
*    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
*    Boston, MA  02110-1301  USA
*
*-

      IMPLICIT NONE

      DOUBLE PRECISION P,H,STL,PV(6)

      DOUBLE PRECISION R,Z,S,C,V

*  Mean sidereal rate (at J2000) in radians per (UT1) second
      DOUBLE PRECISION SR
      PARAMETER (SR=7.292115855306589D-5)



*  Geodetic to geocentric conversion
      CALL sla_GEOC(P,H,R,Z)

*  Functions of ST
      S=SIN(STL)
      C=COS(STL)

*  Speed
      V=SR*R

*  Position
      PV(1)=R*C
      PV(2)=R*S
      PV(3)=Z

*  Velocity
      PV(4)=-V*S
      PV(5)=V*C
      PV(6)=0D0

      END
