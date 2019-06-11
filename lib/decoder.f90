subroutine multimode_decoder(ss,id2,params,nfsample)

  !$ use omp_lib
  use prog_args
  use timer_module, only: timer
  use jt4_decode
  use jt65_decode
  use jt9_decode
  use ft8_decode
  use ft4_decode

  include 'jt9com.f90'
  include 'timer_common.inc'

  type, extends(jt4_decoder) :: counting_jt4_decoder
     integer :: decoded
  end type counting_jt4_decoder

  type, extends(jt65_decoder) :: counting_jt65_decoder
     integer :: decoded
  end type counting_jt65_decoder

  type, extends(jt9_decoder) :: counting_jt9_decoder
     integer :: decoded
  end type counting_jt9_decoder

  type, extends(ft8_decoder) :: counting_ft8_decoder
     integer :: decoded
  end type counting_ft8_decoder

  type, extends(ft4_decoder) :: counting_ft4_decoder
     integer :: decoded
  end type counting_ft4_decoder

  real ss(184,NSMAX)
  logical baddata,newdat65,newdat9,single_decode,bVHF,bad0,newdat,ex
  integer*2 id2(NTMAX*12000)
  type(params_block) :: params
  real*4 dd(NTMAX*12000)
  character(len=20) :: datetime
  character(len=12) :: mycall, hiscall
  character(len=6) :: mygrid, hisgrid
  save
  type(counting_jt4_decoder) :: my_jt4
  type(counting_jt65_decoder) :: my_jt65
  type(counting_jt9_decoder) :: my_jt9
  type(counting_ft8_decoder) :: my_ft8
  type(counting_ft4_decoder) :: my_ft4

  !cast C character arrays to Fortran character strings
  datetime=transfer(params%datetime, datetime)
  mycall=transfer(params%mycall,mycall)
  hiscall=transfer(params%hiscall,hiscall)
  mygrid=transfer(params%mygrid,mygrid)
  hisgrid=transfer(params%hisgrid,hisgrid)

  ! initialize decode counts
  my_jt4%decoded = 0
  my_jt65%decoded = 0
  my_jt9%decoded = 0
  my_ft8%decoded = 0
  my_ft4%decoded = 0

  single_decode=iand(params%nexp_decode,32).ne.0
  bVHF=iand(params%nexp_decode,64).ne.0
  if(mod(params%nranera,2).eq.0) ntrials=10**(params%nranera/2)
  if(mod(params%nranera,2).eq.1) ntrials=3*10**(params%nranera/2)
  if(params%nranera.eq.0) ntrials=0
  
  nfail=0
10 if (params%nagain) then
     open(13,file=trim(temp_dir)//'/decoded.txt',status='unknown',            &
          position='append',iostat=ios)
  else
     open(13,file=trim(temp_dir)//'/decoded.txt',status='unknown',iostat=ios)
  endif
  if(ios.ne.0) then
     nfail=nfail+1
     if(nfail.le.3) then
        call sleep_msec(10)
        go to 10
     endif
  endif

  if(params%nmode.eq.8) then
! We're in FT8 mode
     
     if(ncontest.eq.5) then
! Fox mode: initialize and open houndcallers.txt     
        inquire(file=trim(temp_dir)//'/houndcallers.txt',exist=ex)
        if(.not.ex) then
           c2fox='            '
           g2fox='    '
           nsnrfox=-99
           nfreqfox=-99
           n30z=0
           nwrap=0
           nfox=0
        endif
        open(19,file=trim(temp_dir)//'/houndcallers.txt',status='unknown')
     endif

     call timer('decft8  ',0)
     newdat=params%newdat
     ncontest=iand(params%nexp_decode,7)
     call my_ft8%decode(ft8_decoded,id2,params%nQSOProgress,params%nfqso,    &
          params%nftx,newdat,params%nutc,params%nfa,params%nfb,              &
          params%ndepth,ncontest,logical(params%nagain),                     &
          logical(params%lft8apon),logical(params%lapcqonly),                &
          params%napwid,mycall,hiscall,hisgrid)
     call timer('decft8  ',1)
     if(nfox.gt.0) then
        n30min=minval(n30fox(1:nfox))
        n30max=maxval(n30fox(1:nfox))
     endif
     j=0

     if(ncontest.eq.5) then
! Fox mode: save decoded Hound calls for possible selection by FoxOp
        rewind 19
        if(nfox.eq.0) then
           endfile 19
           rewind 19
        else
           do i=1,nfox
              n=n30fox(i)
              if(n30max-n30fox(i).le.4) then
                 j=j+1
                 c2fox(j)=c2fox(i)
                 g2fox(j)=g2fox(i)
                 nsnrfox(j)=nsnrfox(i)
                 nfreqfox(j)=nfreqfox(i)
                 n30fox(j)=n
                 m=n30max-n
                 if(len(trim(g2fox(j))).eq.4) then
                    call azdist(mygrid,g2fox(j)//'  ',0.d0,nAz,nEl,nDmiles,	&
                         nDkm,nHotAz,nHotABetter)
                 else
                    nDkm=9999
                 endif
                 write(19,1004) c2fox(j),g2fox(j),nsnrfox(j),nfreqfox(j),nDkm,m
1004             format(a12,1x,a4,i5,i6,i7,i3)
              endif
           enddo
           nfox=j
           flush(19)
        endif
     endif
     go to 800
  endif

  if(params%nmode.eq.5) then
     call timer('decft4  ',0)
     call my_ft4%decode(ft4_decoded,id2,params%nQSOProgress,params%nfqso,    &
          params%nutc,params%nfa,params%nfb,params%ndepth,                   &
          logical(params%lapcqonly),ncontest,mycall,hiscall)
     call timer('decft4  ',1)
     go to 800
  endif

  rms=sqrt(dot_product(float(id2(300000:310000)),            &
       float(id2(300000:310000)))/10000.0)
  if(rms.lt.2.0) go to 800

! Zap data at start that might come from T/R switching transient?
  nadd=100
  k=0
  bad0=.false.
  do i=1,240
     sq=0.
     do n=1,nadd
        k=k+1
        sq=sq + float(id2(k))**2
     enddo
     rms=sqrt(sq/nadd)
     if(rms.gt.10000.0) then
        bad0=.true.
        kbad=k
        rmsbad=rms
     endif
  enddo
  if(bad0) then
     nz=min(NTMAX*12000,kbad+100)
!     id2(1:nz)=0                ! temporarily disabled as it can breaak the JT9 decoder, maybe others
  endif
  
  if(params%nmode.eq.4 .or. params%nmode.eq.65) open(14,file=trim(temp_dir)// &
       '/avemsg.txt',status='unknown')
  if(params%nmode.eq.164) open(17,file=trim(temp_dir)//'/red.dat',          &
       status='unknown')

  if(params%nmode.eq.4) then
     jz=52*nfsample
     if(params%newdat) then
        if(nfsample.eq.12000) call wav11(id2,jz,dd)
        if(nfsample.eq.11025) dd(1:jz)=id2(1:jz)
     endif
     call my_jt4%decode(jt4_decoded,dd,jz,params%nutc,params%nfqso,         &
          params%ntol,params%emedelay,params%dttol,logical(params%nagain),  &
          params%ndepth,logical(params%nclearave),params%minsync,           &
          params%minw,params%nsubmode,mycall,hiscall,         &
          hisgrid,params%nlist,params%listutc,jt4_average)
     go to 800
  endif

  npts65=52*12000
  if(params%nmode.eq.164) npts65=54*12000
  if(baddata(id2,npts65)) then
     nsynced=0
     ndecoded=0
     go to 800
  endif
 
  ntol65=params%ntol              !### is this OK? ###
  newdat65=params%newdat
  newdat9=params%newdat

!$call omp_set_dynamic(.true.)
!$omp parallel sections num_threads(2) copyin(/timer_private/) shared(ndecoded) if(.true.) !iif() needed on Mac

!$omp section
  if(params%nmode.eq.65 .or. params%nmode.eq.164 .or.                      &
       (params%nmode.eq.(65+9) .and. params%ntxmode.eq.65)) then
! We're in JT65 or QRA64 mode, or should do JT65 first

     if(newdat65) dd(1:npts65)=id2(1:npts65)
     nf1=params%nfa
     nf2=params%nfb
     call timer('jt65a   ',0)
     call my_jt65%decode(jt65_decoded,dd,npts65,newdat65,params%nutc,      &
          nf1,nf2,params%nfqso,ntol65,params%nsubmode,params%minsync,      &
          logical(params%nagain),params%n2pass,logical(params%nrobust),    &
          ntrials,params%naggressive,params%ndepth,params%emedelay,        &
          logical(params%nclearave),mycall,hiscall,          &
          hisgrid,params%nexp_decode,params%nQSOProgress,           &
          logical(params%ljt65apon))
     call timer('jt65a   ',1)

  else if(params%nmode.eq.9 .or. (params%nmode.eq.(65+9) .and. params%ntxmode.eq.9)) then
! We're in JT9 mode, or should do JT9 first
     call timer('decjt9  ',0)
     call my_jt9%decode(jt9_decoded,ss,id2,params%nfqso,       &
          newdat9,params%npts8,params%nfa,params%nfsplit,params%nfb,       &
          params%ntol,params%nzhsym,logical(params%nagain),params%ndepth,  &
          params%nmode,params%nsubmode,params%nexp_decode)
     call timer('decjt9  ',1)
  endif

!$omp section
  if(params%nmode.eq.(65+9)) then       !Do the other mode (we're in dual mode)
     if (params%ntxmode.eq.9) then
        if(newdat65) dd(1:npts65)=id2(1:npts65)
        nf1=params%nfa
        nf2=params%nfb
        call timer('jt65a   ',0)
        call my_jt65%decode(jt65_decoded,dd,npts65,newdat65,params%nutc,   &
             nf1,nf2,params%nfqso,ntol65,params%nsubmode,params%minsync,   &
             logical(params%nagain),params%n2pass,logical(params%nrobust), &
             ntrials,params%naggressive,params%ndepth,params%emedelay,     &
             logical(params%nclearave),mycall,hiscall,       &
             hisgrid,params%nexp_decode,params%nQSOProgress,        &
             logical(params%ljt65apon))
        call timer('jt65a   ',1)
     else
        call timer('decjt9  ',0)
        call my_jt9%decode(jt9_decoded,ss,id2,params%nfqso,                &
             newdat9,params%npts8,params%nfa,params%nfsplit,params%nfb,    &
             params%ntol,params%nzhsym,logical(params%nagain),             &
             params%ndepth,params%nmode,params%nsubmode,params%nexp_decode)
        call timer('decjt9  ',1)
     end if
  endif

!$omp end parallel sections

! JT65 is not yet producing info for nsynced, ndecoded.
800 ndecoded = my_jt4%decoded + my_jt65%decoded + my_jt9%decoded +       &
         my_ft8%decoded + my_ft4%decoded
  write(*,1010) nsynced,ndecoded
1010 format('<DecodeFinished>',2i4)
  call flush(6)
  close(13)
  if(ncontest.eq.5) close(19)
  if(params%nmode.eq.4 .or. params%nmode.eq.65) close(14)

  return

contains

  subroutine jt4_decoded(this,snr,dt,freq,have_sync,sync,is_deep,    &
       decoded0,qual,ich,is_average,ave)
    implicit none
    class(jt4_decoder), intent(inout) :: this
    integer, intent(in) :: snr
    real, intent(in) :: dt
    integer, intent(in) :: freq
    logical, intent(in) :: have_sync
    logical, intent(in) :: is_deep
    character(len=1), intent(in) :: sync
    character(len=22), intent(in) :: decoded0
    real, intent(in) :: qual
    integer, intent(in) :: ich
    logical, intent(in) :: is_average
    integer, intent(in) :: ave

    character*22 decoded
    character*3 cflags

    if(ich.eq.-99) stop                         !Silence compiler warning
    if (have_sync) then
       decoded=decoded0
       cflags='   '
       if(decoded.ne.'                      ') cflags='f  '
       if(is_deep) then
          cflags(1:2)='d1'
          write(cflags(3:3),'(i1)') min(int(qual),9)
          if(qual.ge.10.0) cflags(3:3)='*'
          if(qual.lt.3.0) decoded(22:22)='?'
       endif
       if(is_average) then
          write(cflags(2:2),'(i1)') min(ave,9)
          if(ave.ge.10) cflags(2:2)='*'
       endif
       write(*,1000) params%nutc,snr,dt,freq,sync,decoded,cflags
1000   format(i4.4,i4,f5.1,i5,1x,'$',a1,1x,a22,1x,a3)
    else
       write(*,1000) params%nutc,snr,dt,freq
    end if

    select type(this)
    type is (counting_jt4_decoder)
       this%decoded = this%decoded + 1
    end select
  end subroutine jt4_decoded

  subroutine jt4_average (this, used, utc, sync, dt, freq, flip)
    implicit none
    class(jt4_decoder), intent(inout) :: this
    logical, intent(in) :: used
    integer, intent(in) :: utc
    real, intent(in) :: sync
    real, intent(in) :: dt
    integer, intent(in) :: freq
    logical, intent(in) :: flip
    character(len=1) :: cused, csync

    cused = '.'
    csync = '*'
    if (used) cused = '$'
    if (flip) csync = '$'
    write(14,1000) cused,utc,sync,dt,freq,csync
1000 format(a1,i5.4,f6.1,f6.2,i6,1x,a1)
  end subroutine jt4_average

  subroutine jt65_decoded(this,sync,snr,dt,freq,drift,nflip,width,     &
       decoded0,ft,qual,nsmo,nsum,minsync)

    use jt65_decode
    implicit none

    class(jt65_decoder), intent(inout) :: this
    real, intent(in) :: sync
    integer, intent(in) :: snr
    real, intent(in) :: dt
    integer, intent(in) :: freq
    integer, intent(in) :: drift
    integer, intent(in) :: nflip
    real, intent(in) :: width
    character(len=22), intent(in) :: decoded0
    integer, intent(in) :: ft
    integer, intent(in) :: qual
    integer, intent(in) :: nsmo
    integer, intent(in) :: nsum
    integer, intent(in) :: minsync

    integer i,nap,nft
    logical is_deep,is_average
    character decoded*22,csync*2,cflags*3

    if(width.eq.-9999.0) stop              !Silence compiler warning
!$omp critical(decode_results)
    decoded=decoded0
    cflags='   '
    is_deep=ft.eq.2

    if(ft.ge.80) then                      !QRA64 mode
       nft=ft-100
       csync=': '
       if(sync-3.4.ge.float(minsync) .or. nft.ge.0) csync=':*'
       if(nft.lt.0) then
          write(*,1009) params%nutc,snr,dt,freq,csync,decoded
       else
          write(*,1009) params%nutc,snr,dt,freq,csync,decoded,nft
1009      format(i4.4,i4,f5.1,i5,1x,a2,1x,a22,i2)
       endif
       write(13,1011) params%nutc,nint(sync),snr,dt,float(freq),drift,    &
            decoded,nft
1011   format(i4.4,i4,i5,f6.2,f8.0,i4,3x,a22,' QRA64',i3)
       go to 100
    endif
    
    if(ft.eq.0 .and. minsync.ge.0 .and. int(sync).lt.minsync) then
       write(*,1010) params%nutc,snr,dt,freq
    else
       is_average=nsum.ge.2
       if(bVHF .and. ft.gt.0) then
          cflags='f  '
          if(is_deep) then
             cflags(1:2)='d1'
             write(cflags(3:3),'(i1)') min(qual,9)
             if(qual.ge.10) cflags(3:3)='*'
             if(qual.lt.3) decoded(22:22)='?'
          endif
          if(is_average) then
             write(cflags(2:2),'(i1)') min(nsum,9)
             if(nsum.ge.10) cflags(2:2)='*'
          endif
          nap=ishft(ft,-2)
          if(nap.ne.0) then
            write(cflags(1:3),'(a1,i1)') 'a',nap 
          endif
       endif
       csync='# '
       i=0
       if(bVHF .and. nflip.ne.0 .and.                         &
            sync.ge.max(0.0,float(minsync))) then
          csync='#*'
          if(nflip.eq.-1) then
             csync='##'
             if(decoded.ne.'                      ') then
                do i=22,1,-1
                   if(decoded(i:i).ne.' ') exit
                enddo
                if(i.gt.18) i=18
                decoded(i+2:i+4)='OOO'
             endif
          endif
       endif
       write(*,1010) params%nutc,snr,dt,freq,csync,decoded,cflags
1010   format(i4.4,i4,f5.1,i5,1x,a2,1x,a22,1x,a3)
    endif
    write(13,1012) params%nutc,nint(sync),snr,dt,float(freq),drift,    &
         decoded,ft,nsum,nsmo
1012 format(i4.4,i4,i5,f6.2,f8.0,i4,3x,a22,' JT65',3i3)

100 call flush(6)

!$omp end critical(decode_results)
    select type(this)
    type is (counting_jt65_decoder)
       this%decoded = this%decoded + 1
    end select
  end subroutine jt65_decoded

  subroutine jt9_decoded (this, sync, snr, dt, freq, drift, decoded)
    use jt9_decode
    implicit none

    class(jt9_decoder), intent(inout) :: this
    real, intent(in) :: sync
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    integer, intent(in) :: drift
    character(len=22), intent(in) :: decoded

    !$omp critical(decode_results)
    write(*,1000) params%nutc,snr,dt,nint(freq),decoded
1000 format(i4.4,i4,f5.1,i5,1x,'@ ',1x,a22)
    write(13,1002) params%nutc,nint(sync),snr,dt,freq,drift,decoded
1002 format(i4.4,i4,i5,f6.1,f8.0,i4,3x,a22,' JT9')
    call flush(6)
    !$omp end critical(decode_results)
    select type(this)
    type is (counting_jt9_decoder)
       this%decoded = this%decoded + 1
    end select
  end subroutine jt9_decoded

  subroutine ft8_decoded (this,sync,snr,dt,freq,decoded,nap,qual)
    use ft8_decode
    implicit none

    class(ft8_decoder), intent(inout) :: this
    real, intent(in) :: sync
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    character c1*12,c2*12,g2*4,w*4
    integer i0,i1,i2,i3,i4,i5,n30,nwrap
    integer, intent(in) :: nap 
    real, intent(in) :: qual 
    character*2 annot
    character*37 decoded0
    logical isgrid4,first,b0,b1,b2
    data first/.true./
    save

    isgrid4(w)=(len_trim(w).eq.4 .and.                                        &
         ichar(w(1:1)).ge.ichar('A') .and. ichar(w(1:1)).le.ichar('R') .and.  &
         ichar(w(2:2)).ge.ichar('A') .and. ichar(w(2:2)).le.ichar('R') .and.  &
         ichar(w(3:3)).ge.ichar('0') .and. ichar(w(3:3)).le.ichar('9') .and.  &
         ichar(w(4:4)).ge.ichar('0') .and. ichar(w(4:4)).le.ichar('9'))

    if(first) then
       c2fox='            '
       g2fox='    '
       nsnrfox=-99
       nfreqfox=-99
       n30z=0
       nwrap=0
       nfox=0
       first=.false.
    endif
    
    decoded0=decoded

    annot='  ' 
    if(nap.ne.0) then
       write(annot,'(a1,i1)') 'a',nap
       if(qual.lt.0.17) decoded0(37:37)='?'
    endif

!    i0=index(decoded0,';')
! Always print 37 characters? Or, send i3,n3 up to here from ft8b_2 and use them
! to decide how many chars to print?
!TEMP
    i0=1
    if(i0.le.0) write(*,1000) params%nutc,snr,dt,nint(freq),decoded0(1:22),annot
1000 format(i6.6,i4,f5.1,i5,' ~ ',1x,a22,1x,a2)
    if(i0.gt.0) write(*,1001) params%nutc,snr,dt,nint(freq),decoded0,annot
1001 format(i6.6,i4,f5.1,i5,' ~ ',1x,a37,1x,a2)
    write(13,1002) params%nutc,nint(sync),snr,dt,freq,0,decoded0
1002 format(i6.6,i4,i5,f6.1,f8.0,i4,3x,a37,' FT8')

    if(ncontest.eq.5) then
       i1=index(decoded0,' ')
       i2=i1 + index(decoded0(i1+1:),' ')
       i3=i2 + index(decoded0(i2+1:),' ')
       if(i1.ge.3 .and. i2.ge.7 .and. i3.ge.10) then
          c1=decoded0(1:i1-1)//'            '
          c2=decoded0(i1+1:i2-1)
          g2=decoded0(i2+1:i3-1)
          b0=c1.eq.mycall
          if(c1(1:3).eq.'DE ' .and. index(c2,'/').ge.2) b0=.true.
          if(len(trim(c1)).ne.len(trim(mycall))) then
             i4=index(trim(c1),trim(mycall))
             i5=index(trim(mycall),trim(c1))
             if(i4.ge.1 .or. i5.ge.1) b0=.true.
          endif
          b1=i3-i2.eq.5 .and. isgrid4(g2)
          b2=i3-i2.eq.1
          if(b0 .and. (b1.or.b2) .and. nint(freq).ge.1000) then
             n=params%nutc
             n30=(3600*(n/10000) + 60*mod((n/100),100) + mod(n,100))/30
             if(n30.lt.n30z) nwrap=nwrap+5760    !New UTC day, handle the wrap
             n30z=n30
             n30=n30+nwrap
             if(nfox.lt.MAXFOX) nfox=nfox+1
             c2fox(nfox)=c2
             g2fox(nfox)=g2
             nsnrfox(nfox)=snr
             nfreqfox(nfox)=nint(freq)
             n30fox(nfox)=n30
          endif
       endif
    endif
    
    call flush(6)
    call flush(13)
    
    select type(this)
    type is (counting_ft8_decoder)
       this%decoded = this%decoded + 1
    end select

    return
  end subroutine ft8_decoded

  subroutine ft4_decoded (this,sync,snr,dt,freq,decoded,nap,qual)
    use ft4_decode
    implicit none

    class(ft4_decoder), intent(inout) :: this
    real, intent(in) :: sync
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    character c1*12,c2*12,g2*4,w*4
    integer i0,i1,i2,i3,i4,i5,n30,nwrap
    integer, intent(in) :: nap 
    real, intent(in) :: qual 
    character*2 annot
    character*37 decoded0
    
    decoded0=decoded

    annot='  ' 
    if(nap.ne.0) then
       write(annot,'(a1,i1)') 'a',nap
       if(qual.lt.0.17) decoded0(37:37)='?'
    endif

    write(*,1001) params%nutc,snr,dt,nint(freq),decoded0,annot
1001 format(i6.6,i4,f5.1,i5,' + ',1x,a37,1x,a2)
    write(13,1002) params%nutc,nint(sync),snr,dt,freq,0,decoded0
1002 format(i6.6,i4,i5,f6.1,f8.0,i4,3x,a37,' FT4')
    
    call flush(6)
    call flush(13)
    
    select type(this)
    type is (counting_ft4_decoder)
       this%decoded = this%decoded + 1
    end select

    return
  end subroutine ft4_decoded
end subroutine multimode_decoder
