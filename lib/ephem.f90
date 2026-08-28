subroutine ephem(mjd0,dut,east_long,geodetic_lat,height,nspecial,     &
     RA,Dec,Az,El,techo,dop,fspread_1GHz,vr,ephemeris_result)

  use ieee_arithmetic, only: ieee_is_finite
  use jpl_ephemeris_status, only: JPL_STATUS_OK, EPHEMERIS_JPL,      &
       EPHEMERIS_ANALYTIC_FALLBACK, EPHEMERIS_UNAVAILABLE
  implicit real*8 (a-h,o-z)
  real*8 jd
  real*8 mjd,mjd0
  real*8 prec(3,3)
  real*8 rmatn(3,3)
  real*8 rme2000(6,2)
  real*8 rmeDate(6)
  real*8 rmeTrue(6)
  real*8 raeTrue(6)
  real*8 rmaTrue(6)
  real*8 djutc_sample(2),jd_sample(2),djtt_sample(2)
  real*8, parameter :: max_lunar_position_km=1.d7
  real*8, parameter :: max_lunar_velocity_km_s=100.d0
  logical km,bary,jpl_available,use_jpl,derived_valid
  integer ephemeris_result,jpl_status,attempt
  common/stcomx/km,bary,pvsun(6)
  common/librcom/xl(2),b(2)

  RA=0.d0
  Dec=0.d0
  Az=0.d0
  El=0.d0
  techo=0.d0
  dop=0.d0
  fspread_1GHz=0.d0
  vr=0.d0
  ephemeris_result=EPHEMERIS_UNAVAILABLE

  twopi=8.d0*atan(1.d0)
  rad=360.d0/twopi
  clight=2.99792458d5
  au2km=0.1495978706910000d9
  km=.true.
  freq=1000.0d6

  jpl_available=nspecial.ne.8
  do jj=1,2
     mjd=mjd0
     if(jj.eq.1) mjd=mjd - 1.d0/1440.d0
     djutc_sample(jj)=mjd
     jd_sample(jj)=2400000.5d0 + mjd
     djtt_sample(jj)=mjd + sla_DTT(jd_sample(jj))/86400.d0

     if(jpl_available) then
        ttjd=2400000.5d0 + djtt_sample(jj)
        call pleph(ttjd,10,3,rme2000(:,jj),jpl_status)
        if(jpl_status.ne.JPL_STATUS_OK) jpl_available=.false.
        if(jpl_available) then
           if(.not.all(ieee_is_finite(rme2000(:,jj))))             &
                jpl_available=.false.
        endif
        if(jpl_available) then
           if(any(abs(rme2000(1:3,jj)).gt.max_lunar_position_km)   &
                .or. any(abs(rme2000(4:6,jj)).gt.                  &
                max_lunar_velocity_km_s))                          &
                jpl_available=.false.
        endif
     endif
  enddo

  do attempt=1,2
     use_jpl=jpl_available
     derived_valid=.true.

     do jj=1,2
        djutc=djutc_sample(jj)
        jd=jd_sample(jj)
        djtt=djtt_sample(jj)

        if(use_jpl) then
           year=2000.d0 + (jd-2451545.d0)/365.25d0
           call sla_PREC(2000.0d0,year,prec)
           rmeDate(1:3)=matmul(prec,rme2000(1:3,jj))
           rmeDate(4:6)=matmul(prec,rme2000(4:6,jj))
        else
           call sla_DMOON(djtt,rmeDate)
           rmeDate=rmeDate*au2km
        endif
        if(.not.all(ieee_is_finite(rmeDate))) then
           derived_valid=.false.
           exit
        endif

        if(nspecial.eq.7) then
           rmeTrue=rmeDate
        else
           call sla_NUT(djtt,rmatn)
           call sla_DMXV(rmatn,rmeDate,rmeTrue)
           call sla_DMXV(rmatn,rmeDate(4),rmeTrue(4))
        endif
        if(.not.all(ieee_is_finite(rmeTrue))) then
           derived_valid=.false.
           exit
        endif

        djut1=djutc + dut/86400.d0
        if(nspecial.eq.6) djut1=djutc
        xlast=sla_DRANRM(sla_GMST(djut1) + sla_EQEQX(djtt) +        &
             east_long)
        call sla_PVOBS(geodetic_lat,height,xlast,raeTrue)
        rmaTrue=rmeTrue - raeTrue*au2km
        if(.not.ieee_is_finite(xlast) .or.                           &
             .not.all(ieee_is_finite(raeTrue)) .or.                 &
             .not.all(ieee_is_finite(rmaTrue))) then
           derived_valid=.false.
           exit
        endif

        if(nspecial.ne.2) then
           tl=499.004782D0*SQRT(rmaTrue(1)**2 + rmaTrue(2)**2 +     &
                rmaTrue(3)**2)
           rmaTrue(1:3)=rmaTrue(1:3)-tl*rmaTrue(4:6)/au2km
           if(.not.ieee_is_finite(tl) .or.                          &
                .not.all(ieee_is_finite(rmaTrue))) then
              derived_valid=.false.
              exit
           endif
        endif

        call sla_DC62S(rmaTrue,RA,Dec,dist,RAdot,DECdot,vr)
        dop=-2.d0 * freq * vr/clight
        techo=2.d0*dist/clight
        call libration(jd,RA,Dec,xl(jj),b(jj))
        if(.not.all(ieee_is_finite((/RA,Dec,dist,RAdot,DECdot,vr,   &
             dop,techo,xl(jj),b(jj)/)))) then
           derived_valid=.false.
           exit
        endif
     enddo

     if(derived_valid) then
        dldt=57.2957795131*(xl(2)-xl(1))
        dbdt=57.2957795131*(b(2)-b(1))
        rate=sqrt((2*dldt)**2 + (2*dbdt)**2)
        fspread_1GHz=0.5*6741*rate
        call sla_DE2H(xlast-RA,Dec,geodetic_lat,Az,El)
        if(.not.all(ieee_is_finite((/dldt,dbdt,rate,fspread_1GHz,   &
             Az,El/)))) derived_valid=.false.
     endif

     if(use_jpl .and. .not.derived_valid) then
        jpl_available=.false.
        cycle
     endif
     exit
  enddo

  if(derived_valid) then
     if(use_jpl) then
        ephemeris_result=EPHEMERIS_JPL
     else
        ephemeris_result=EPHEMERIS_ANALYTIC_FALLBACK
     endif
  else
     ephemeris_result=EPHEMERIS_UNAVAILABLE
     RA=0.d0
     Dec=0.d0
     Az=0.d0
     El=0.d0
     techo=0.d0
     dop=0.d0
     fspread_1GHz=0.d0
     vr=0.d0
     xl=0.d0
     b=0.d0
  endif

end subroutine ephem
