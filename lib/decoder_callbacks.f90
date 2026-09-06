module decoder_callbacks

  use jt4_decode
  use jt65_decode
  use jt9_decode
  use ft8_decode
  use ft8_decodevar
  use ft4_decode
  use fst4_decode
  use q65_decode
  use streaming_emit, only: streaming_emit_enabled, streaming_emit_decode

  implicit none

  type :: decoder_callback_context
     ! Callback state is stored on each decoder instead of captured by a nested procedure.
     integer :: nutc = 0
     integer :: nfqso = 0
     integer :: ncontest = 0
     integer :: ios13 = -1
     logical :: bVHF = .false.
     logical :: b_superfox = .false.
     character(len=12) :: mycall = '            '
  end type decoder_callback_context

  type, extends(jt4_decoder) :: counting_jt4_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
  end type counting_jt4_decoder

  type, extends(jt65_decoder) :: counting_jt65_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
  end type counting_jt65_decoder

  type, extends(jt9_decoder) :: counting_jt9_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
  end type counting_jt9_decoder

  type, extends(ft8_decoder) :: counting_ft8_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
  end type counting_ft8_decoder

  type, extends(ft8_decodervar) :: counting_ft8_decodervar
     type(decoder_callback_context) :: context
     integer :: decodedvar = 0
     real :: xdtt(200)
  end type counting_ft8_decodervar

  type, extends(ft4_decoder) :: counting_ft4_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
  end type counting_ft4_decoder

  type, extends(fst4_decoder) :: counting_fst4_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
  end type counting_fst4_decoder

  type, extends(q65_decoder) :: counting_q65_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
  end type counting_q65_decoder

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
    integer context_nutc

    select type (typed_this => this)
    type is (counting_jt4_decoder)
       context_nutc = typed_this%context%nutc
    class default
       return
    end select

    if(ich.eq.-99) stop
    if (have_sync) then
       decoded=decoded0
       cflags='   '
       if(decoded.ne.'                      ') then
          cflags='f  '
          if(is_deep) then
             cflags='d  '
             write(cflags(2:2),'(i1)') min(int(qual),9)
             if(qual.ge.10.0) cflags(2:2)='*'
             if(qual.lt.3.0) decoded(22:22)='?'
          endif
          if(is_average) then
             write(cflags(3:3),'(i1)') min(ave,9)
             if(ave.ge.10) cflags(3:3)='*'
             if(cflags(1:1).eq.'f') cflags=cflags(1:1)//cflags(3:3)//' '
          endif
       endif
       if (streaming_emit_enabled()) then
          call streaming_emit_decode("JT4", context_nutc, snr, dt, freq, decoded)
       else
          write(*,1000) context_nutc,snr,dt,freq,sync,decoded,cflags
       end if
    else
       if (.not. streaming_emit_enabled()) write(*,1000) context_nutc,snr,dt,freq
    end if
1000 format(i4.4,i4,f5.1,i5,1x,'$',a1,1x,a22,1x,a3)

    select type (typed_this => this)
    type is (counting_jt4_decoder)
       typed_this%decoded = typed_this%decoded + 1
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

    integer i,nap,n
    integer context_nutc, context_ios13
    logical context_bVHF
    logical is_deep,is_average
    character decoded*22,csync*2,cflags*3

    select type (typed_this => this)
    type is (counting_jt65_decoder)
       context_nutc = typed_this%context%nutc
       context_ios13 = typed_this%context%ios13
       context_bVHF = typed_this%context%bVHF
    class default
       return
    end select

    !$omp critical(decode_results)
    decoded=decoded0
    cflags='   '
    is_deep=ft.eq.2

    if(ft.eq.0 .and. minsync.ge.0 .and. int(sync).lt.minsync) then
       if (.not. streaming_emit_enabled()) write(*,1010) context_nutc,snr,dt,freq
    else
       is_average=nsum.ge.2
       if(context_bVHF .and. ft.gt.0) then
          cflags='f  '
          if(is_deep) then
             cflags='d  '
             write(cflags(2:2),'(i1)') min(qual,9)
             if(qual.ge.10) cflags(2:2)='*'
             if(qual.lt.3) decoded(22:22)='?'
          endif
          if(is_average) then
             write(cflags(3:3),'(i1)') min(nsum,9)
             if(nsum.ge.10) cflags(3:3)='*'
          endif
          nap=ishft(ft,-2)
          if(nap.ne.0) then
             if(nsum.lt.2) write(cflags(1:3),'(a1,i1," ")') 'a',nap
             if(nsum.ge.2) write(cflags(1:3),'(a1,2i1)') 'a',nap,min(nsum,9)
          endif
       endif
       csync='# '
       i=0
       if(context_bVHF .and. nflip.ne.0 .and.                         &
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
       n=len(trim(decoded))
       if(n.eq.2 .or. n.eq.3) csync='# '
       if(cflags(1:1).eq.'f') then
          cflags(2:2)=cflags(3:3)
          cflags(3:3)=' '
       endif
       if (streaming_emit_enabled()) then
          call streaming_emit_decode("JT65", context_nutc, snr, dt, freq, decoded)
       else
          write(*,1010) context_nutc,snr,dt,freq,csync,decoded,cflags
       end if
1010   format(i4.4,i4,f5.1,i5,1x,a2,1x,a22,1x,a3)
    endif
    if(context_ios13.eq.0) write(13,1012) context_nutc,nint(sync),snr,dt,    &
         float(freq),drift,decoded,ft,nsum,nsmo
1012 format(i4.4,i4,i5,f6.2,f8.0,i4,3x,a22,' JT65',3i3)
    call flush(6)

    !$omp end critical(decode_results)
    select type (typed_this => this)
    type is (counting_jt65_decoder)
       typed_this%decoded = typed_this%decoded + 1
    end select
  end subroutine jt65_decoded

  subroutine jt9_decoded (this, sync, snr, dt, freq, drift, decoded)
    implicit none

    class(jt9_decoder), intent(inout) :: this
    real, intent(in) :: sync
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    integer, intent(in) :: drift
    character(len=22), intent(in) :: decoded
    integer context_nutc, context_ios13

    select type (typed_this => this)
    type is (counting_jt9_decoder)
       context_nutc = typed_this%context%nutc
       context_ios13 = typed_this%context%ios13
    class default
       return
    end select

    !$omp critical(decode_results)

    if (streaming_emit_enabled()) then
       call streaming_emit_decode("JT9", context_nutc, snr, dt, nint(freq), decoded)
    else
       write(*,1000) context_nutc,snr,dt,nint(freq),decoded
    end if
1000 format(i4.4,i4,f5.1,i5,1x,'@ ',1x,a22)
    if(context_ios13.eq.0) write(13,1002) context_nutc,nint(sync),snr,dt,freq,  &
         drift,decoded
1002 format(i4.4,i4,i5,f6.1,f8.0,i4,3x,a22,' JT9')
    call flush(6)
    !$omp end critical(decode_results)
    select type (typed_this => this)
    type is (counting_jt9_decoder)
       typed_this%decoded = typed_this%decoded + 1
    end select
  end subroutine jt9_decoded

  subroutine ft8_decodedvar  (this,snr,dt,freq,decodedvar,nap,qual)
    implicit none

    class(ft8_decodervar), intent(inout) :: this
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    real, intent(in) :: qual
    character(len=37), intent(in) :: decodedvar
    integer, intent(in) :: nap
    real fdiff
    character*2 annot
    character c1*12,c2*12,g2*4,w*4
    integer i1,i2,i3,i4,i5,n,n30,nwrap
    character*37 decoded0
    logical isgrid4,first,b0,b1,b2
    integer context_nutc, context_nfqso, context_ncontest, context_ios13
    logical context_b_superfox
    character(len=12) context_mycall
    data first/.true./
    save :: first,nwrap

    isgrid4(w)=(len_trim(w).eq.4 .and.                                        &
         ichar(w(1:1)).ge.ichar('A') .and. ichar(w(1:1)).le.ichar('R') .and.  &
         ichar(w(2:2)).ge.ichar('A') .and. ichar(w(2:2)).le.ichar('R') .and.  &
         ichar(w(3:3)).ge.ichar('0') .and. ichar(w(3:3)).le.ichar('9') .and.  &
         ichar(w(4:4)).ge.ichar('0') .and. ichar(w(4:4)).le.ichar('9'))

    select type (typed_this => this)
    type is (counting_ft8_decodervar)
       context_nutc = typed_this%context%nutc
       context_nfqso = typed_this%context%nfqso
       context_ncontest = typed_this%context%ncontest
       context_ios13 = typed_this%context%ios13
       context_b_superfox = typed_this%context%b_superfox
       context_mycall = typed_this%context%mycall
    class default
       return
    end select

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

    fdiff=freq-context_nfqso
    if(abs(fdiff).lt.3.0) ltry_a8=.false.

    decoded0=decodedvar

    annot='  '
    if(nap.ne.0) then
       if(nap.ge.9) then
          write(annot,'(a1,i1)') 'a',9
       else
          write(annot,'(a1,i1)') 'a',nap
       endif
       if(qual.lt.0.17) decoded0(37:37)='?'
    endif

    if (streaming_emit_enabled()) then
       call streaming_emit_decode("FT8", context_nutc, snr, dt, nint(freq), decoded0)
    else
       write(*,1000) context_nutc,snr,dt,nint(freq),decoded0,annot
    end if
1000 format(i6.6,i4,f5.1,i5,' ~ ',1x,a37,1x,a2)

    if(context_ncontest.eq.6) then
       i1=index(decoded0,' ')
       i2=i1 + index(decoded0(i1+1:),' ')
       i3=i2 + index(decoded0(i2+1:),' ')
       if(i1.ge.3 .and. i2.ge.7 .and. i3.ge.10) then
          c1=decoded0(1:i1-1)//'            '
          c2=decoded0(i1+1:i2-1)
          g2=decoded0(i2+1:i3-1)
          b0=c1.eq.context_mycall
          if(c1(1:3).eq.'DE ' .and. index(c2,'/').ge.2) b0=.true.
          if(len(trim(c1)).ne.len(trim(context_mycall))) then
             i4=index(trim(c1),trim(context_mycall))
             i5=index(trim(context_mycall),trim(c1))
             if(i4.ge.1 .or. i5.ge.1) b0=.true.
          endif
          b1=i3-i2.eq.5 .and. isgrid4(g2)
          b2=i3-i2.eq.1
          if(b0 .and. (b1.or.b2) .and. (nint(freq).ge.1000 .or. &
               context_b_superfox)) then
             n=context_nutc
             n30=(3600*(n/10000) + 60*mod((n/100),100) + mod(n,100))/30
             if(n30.lt.n30z) nwrap=nwrap+2880
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
    if(context_ios13.eq.0) call flush(13)

    select type (typed_this => this)
    type is (counting_ft8_decodervar)
       typed_this%decodedvar = typed_this%decodedvar + 1
       typed_this%xdtt(typed_this%decodedvar)=dt
    end select
  end subroutine ft8_decodedvar

  subroutine ft8_decoded (this,sync,snr,dt,freq,decoded,nap,qual)
    implicit none

    class(ft8_decoder), intent(inout) :: this
    real, intent(in) :: sync
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    character c1*12,c2*12,g2*4,w*4
    integer i0,i1,i2,i3,i4,i5,n,n30,nwrap
    integer, intent(in) :: nap
    real, intent(in) :: qual
    real fdiff
    character*2 annot
    character*37 decoded0
    logical isgrid4,first,b0,b1,b2
    integer context_nutc, context_nfqso, context_ncontest, context_ios13
    logical context_b_superfox
    character(len=12) context_mycall
    data first/.true./
    save :: first,nwrap

    isgrid4(w)=(len_trim(w).eq.4 .and.                                        &
         ichar(w(1:1)).ge.ichar('A') .and. ichar(w(1:1)).le.ichar('R') .and.  &
         ichar(w(2:2)).ge.ichar('A') .and. ichar(w(2:2)).le.ichar('R') .and.  &
         ichar(w(3:3)).ge.ichar('0') .and. ichar(w(3:3)).le.ichar('9') .and.  &
         ichar(w(4:4)).ge.ichar('0') .and. ichar(w(4:4)).le.ichar('9'))

    select type (typed_this => this)
    type is (counting_ft8_decoder)
       context_nutc = typed_this%context%nutc
       context_nfqso = typed_this%context%nfqso
       context_ncontest = typed_this%context%ncontest
       context_ios13 = typed_this%context%ios13
       context_b_superfox = typed_this%context%b_superfox
       context_mycall = typed_this%context%mycall
    class default
       return
    end select

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
    fdiff=freq-context_nfqso
    if(abs(fdiff).lt.3.0) ltry_a8=.false.

    annot='  '
    if(nap.ne.0) then
       write(annot,'(a1,i1)') 'a',nap
       if(qual.lt.0.17) decoded0(37:37)='?'
    endif

    i0=1
    if (streaming_emit_enabled()) then
       if (i0.le.0) call streaming_emit_decode("FT8", context_nutc, snr, dt, nint(freq), decoded0(1:22))
       if (i0.gt.0) call streaming_emit_decode("FT8", context_nutc, snr, dt, nint(freq), decoded0)
    else
       if(i0.le.0) write(*,1000) context_nutc,snr,dt,nint(freq),decoded0(1:22),annot
       if(i0.gt.0) write(*,1001) context_nutc,snr,dt,nint(freq),decoded0,annot
    end if
1000 format(i6.6,i4,f5.1,i5,' ~ ',1x,a22,1x,a2)
1001 format(i6.6,i4,f5.1,i5,' ~ ',1x,a37,1x,a2)
    if(context_ios13.eq.0) write(13,1002) context_nutc,nint(sync),snr,dt,freq,0,decoded0
1002 format(i6.6,i4,i5,f6.1,f8.0,i4,3x,a37,' FT8')

    if(context_ncontest.eq.6) then
       i1=index(decoded0,' ')
       i2=i1 + index(decoded0(i1+1:),' ')
       i3=i2 + index(decoded0(i2+1:),' ')
       if(i1.ge.3 .and. i2.ge.7 .and. i3.ge.10) then
          c1=decoded0(1:i1-1)//'            '
          c2=decoded0(i1+1:i2-1)
          g2=decoded0(i2+1:i3-1)
          b0=c1.eq.context_mycall
          if(c1(1:3).eq.'DE ' .and. index(c2,'/').ge.2) b0=.true.
          if(len(trim(c1)).ne.len(trim(context_mycall))) then
             i4=index(trim(c1),trim(context_mycall))
             i5=index(trim(context_mycall),trim(c1))
             if(i4.ge.1 .or. i5.ge.1) b0=.true.
          endif
          b1=i3-i2.eq.5 .and. isgrid4(g2)
          b2=i3-i2.eq.1
          if(b0 .and. (b1.or.b2) .and. (nint(freq).ge.1000 .or. &
               context_b_superfox)) then
             n=context_nutc
             n30=(3600*(n/10000) + 60*mod((n/100),100) + mod(n,100))/30
             if(n30.lt.n30z) nwrap=nwrap+2880
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
    if(context_ios13.eq.0) call flush(13)

    select type (typed_this => this)
    type is (counting_ft8_decoder)
       typed_this%decoded = typed_this%decoded + 1
    end select
  end subroutine ft8_decoded

  subroutine ft4_decoded (this,sync,snr,dt,freq,decoded,nap,qual)
    implicit none

    class(ft4_decoder), intent(inout) :: this
    real, intent(in) :: sync,dt,freq,qual
    integer, intent(in) :: snr,nap
    character(len=37), intent(in) :: decoded
    character*2 annot
    character*37 decoded0
    integer context_nutc, context_ios13

    select type (typed_this => this)
    type is (counting_ft4_decoder)
       context_nutc = typed_this%context%nutc
       context_ios13 = typed_this%context%ios13
    class default
       return
    end select

    decoded0=decoded

    annot='  '
    if(nap.ne.0) then
       write(annot,'(a1,i1)') 'a',nap
       if(qual.lt.0.17) decoded0(37:37)='?'
    endif

    if (streaming_emit_enabled()) then
       call streaming_emit_decode("FT4", context_nutc, snr, dt, nint(freq), decoded0)
    else
       write(*,1001) context_nutc,snr,dt,nint(freq),decoded0,annot
    end if
1001 format(i6.6,i4,f5.1,i5,' + ',1x,a37,1x,a2)

    if(context_ios13.eq.0) then
       write(13,1002,err=10) context_nutc,nint(sync),snr,dt,freq,0,decoded0
1002   format(i6.6,i4,i5,f6.1,f8.0,i4,3x,a37,' FT4')
       flush(13)
    endif

10  call flush(6)

    select type (typed_this => this)
    type is (counting_ft4_decoder)
       typed_this%decoded = typed_this%decoded + 1
    end select
  end subroutine ft4_decoded

  subroutine fst4_decoded (this,nutc,sync,nsnr,dt,freq,decoded,nap,   &
       qual,ntrperiod,fmid,w50)

    implicit none

    class(fst4_decoder), intent(inout) :: this
    integer, intent(in) :: nutc
    real, intent(in) :: sync
    integer, intent(in) :: nsnr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    integer, intent(in) :: nap
    real, intent(in) :: qual
    integer, intent(in) :: ntrperiod
    real, intent(in) :: fmid
    real, intent(in) :: w50

    character*2 annot
    character*37 decoded0
    character*70 line
    integer context_ios13

    select type (typed_this => this)
    type is (counting_fst4_decoder)
       context_ios13 = typed_this%context%ios13
    class default
       return
    end select

    decoded0=decoded
    annot='  '
    if(nap.ne.0) then
       write(annot,'(a1,i1)') 'a',nap
       if(qual.lt.0.17) decoded0(37:37)='?'
    endif

    if(ntrperiod.lt.60) then
       write(line,1001) nutc,nsnr,dt,nint(freq),decoded0,annot
1001   format(i6.6,i4,f5.1,i5,' ` ',1x,a37,1x,a2)
       if(context_ios13.eq.0) write(13,1002) nutc,nint(sync),nsnr,dt,freq,0,decoded0
1002   format(i6.6,i4,i5,f6.1,f8.0,i4,3x,a37,' FST4')
    else
       write(line,1003) nutc,nsnr,dt,nint(freq),decoded0,annot
1003   format(i4.4,i4,f5.1,i5,' ` ',1x,a37,1x,a2,2f7.3)
       if(context_ios13.eq.0) write(13,1004) nutc,nint(sync),nsnr,dt,freq,0,decoded0
1004   format(i4.4,i4,i5,f6.1,f8.0,i4,3x,a37,' FST4')
    endif

    if(fmid.ne.-999.0) then
       if(w50.lt.0.95) write(line(65:70),'(f6.3)') w50
       if(w50.ge.0.95) write(line(65:70),'(f6.2)') w50
    endif

    if (streaming_emit_enabled()) then
       call streaming_emit_decode("FST4", nutc, nsnr, dt, nint(freq), decoded0)
    else
       write(*,1005) line
    end if
1005 format(a70)

    call flush(6)
    if(context_ios13.eq.0) call flush(13)

    select type (typed_this => this)
    type is (counting_fst4_decoder)
       typed_this%decoded = typed_this%decoded + 1
    end select
  end subroutine fst4_decoded

  subroutine q65_decoded (this,nutc,snr1,nsnr,dt,freq,decoded,idec,   &
       nused,ntrperiod,iflagdec)

    implicit none

    class(q65_decoder), intent(inout) :: this
    integer, intent(in) :: nutc
    real, intent(in) :: snr1
    integer, intent(in) :: nsnr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    integer, intent(in) :: idec
    integer, intent(in) :: nused
    integer, intent(in) :: ntrperiod
    integer, intent(in) :: iflagdec  !Recovered spare 78th bit (see genq65/q65_ap)
    character*4 cflags
    integer context_ios13

    select type (typed_this => this)
    type is (counting_q65_decoder)
       context_ios13 = typed_this%context%ios13
    class default
       return
    end select

    cflags='    '
    if(idec.ge.0) then
       cflags='q   '
       write(cflags(2:2),'(i1)') idec
       if(nused.ge.2) write(cflags(3:3),'(i1)') nused
       ! Append '#' (e.g. "q0#", "q3#") when the sender flagged bit78=1,
       ! copying our last transmission -- leave the qualifier unchanged
       ! ("q0", "q3", ...) otherwise.
       if(iflagdec.eq.1) cflags=trim(cflags)//'#'
    endif

    if(ntrperiod.lt.60) then
       if (streaming_emit_enabled()) then
          call streaming_emit_decode("Q65", nutc, nsnr, dt, nint(freq), decoded)
       else
          write(*,1001) nutc,nsnr,dt,nint(freq),decoded,cflags
       end if
1001   format(i6.6,i4,f5.1,i5,' : ',1x,a37,1x,a4)
       if(context_ios13.eq.0) write(13,1002) nutc,nint(snr1),nsnr,dt,freq,0,decoded
1002   format(i6.6,i4,i5,f6.1,f8.0,i4,3x,a37,' Q65')
    else
       if (streaming_emit_enabled()) then
          call streaming_emit_decode("Q65", nutc, nsnr, dt, nint(freq), decoded)
       else
          write(*,1003) nutc,nsnr,dt,nint(freq),decoded,cflags
       end if
1003   format(i4.4,i4,f5.1,i5,' : ',1x,a37,1x,a4)
       if(context_ios13.eq.0) write(13,1004) nutc,nint(snr1),nsnr,dt,freq,0,decoded
1004   format(i4.4,i4,i5,f6.1,f8.0,i4,3x,a37,' Q65')
    endif
    call flush(6)
    if(context_ios13.eq.0) call flush(13)

    select type (typed_this => this)
    type is (counting_q65_decoder)
       if(idec.ge.0) typed_this%decoded = typed_this%decoded + 1
    end select
  end subroutine q65_decoded

end module decoder_callbacks
