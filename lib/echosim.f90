program echosim

! Generate simulated echo-mode files -- self-echo or "measure" 

  use wavhdr
  use julian
  parameter (NWAVE=27648,NZ=36000)
  type(hdr) h                            !Header for .wav file
  character arg*12,fname*17
  complex c0(0:NZ-1)
  complex c(0:NZ-1)
  real*4 level_1,level_2
  real*8 f0,dt,twopi,phi,dphi
  real wave(NZ)
  integer*2 iwave(NZ)                  !Generated full-length waveform
  character*32 isft                    !WAV LIST/INFO trailer fields
  character*24 icrd
  character*64 icmt
  integer*8 tsec8
  integer gmt(9)
  integer*4 trailerlen,wav_trailer_length
  equivalence (nDop0,iwave(1))
  equivalence (nDopAudio0,iwave(3))
  equivalence (nfrit0,iwave(5))
  equivalence (f10,iwave(7))
  equivalence (fspread0,iwave(9))

! Get command-line argument(s)
  nargs=iargc()
  if(nargs.ne.3 .and. nargs.ne.5) then
     print*,'Usage 1:  echosim   f0   fdop fspread nfiles snr'
     print*,'Example:  echosim  1500   0.0   4.0     10   -22'
     print*,'Usage 2:  echosim level_1 level_2 nfiles'
     print*,'Example:  echosim   30.0    40.0   100'
     go to 999
  endif

  call getarg(1,arg)
  read(arg,*) f0                         !Tone frequency
  call getarg(2,arg)
  read(arg,*) fdop                       !Doppler shift (Hz)
  call getarg(3,arg)
  read(arg,*) fspread             !Frequency spread (Hz) (JHT Lorentzian model)

  if(nargs.eq.3) then
     level_1=f0
     level_2=fdop
     nfiles=fspread
     snrdb=0.
     go to 10
  endif
  
  call getarg(4,arg)
  read(arg,*) nfiles                     !Number of files
  call getarg(5,arg)
  read(arg,*) snrdb                      !SNR_2500

10 twopi=8.d0*atan(1.d0)
  fs=12000.0                             !Sample rate (Hz)
  dt=1.d0/fs                              !Sample interval (s)
  bandwidth_ratio=2500.0/(fs/2.0)
  sig=sqrt(2*bandwidth_ratio) * 10.0**(0.05*snrdb)
  if(snrdb.gt.90.0) sig=1.0
  dphi=twopi*(f0+fdop)*dt

  write(*,1000)
1000 format('   N   f0     fDop fSpread   SNR  File name'/51('-'))

  isft='WSJT-X echosim'
  tsec8=itime8()
  call gmtime(int(tsec8,4),gmt)
  write(icrd,1004) gmt(6)+1900,gmt(5)+1,gmt(4),gmt(3),gmt(2),gmt(1)
1004 format(i4.4,'-',i2.2,'-',i2.2,'T',i2.2,':',i2.2,':',i2.2,'Z')
  write(icmt,1005) nfiles
1005 format('Mode=Echo; Simulated by echosim; nfiles=',i0)
  trailerlen=wav_trailer_length(isft,icrd,icmt)

  do ifile=1,nfiles
     wave=0.

     if(nargs.eq.5) then
        phi=0.d0
        do i=0,NWAVE-1
           phi=phi + dphi
           if(phi.gt.twopi) phi=phi-twopi
           xphi=phi
           c0(i)=cmplx(cos(xphi),sin(xphi))
        enddo
        c0(NWAVE:)=0.
        if(fspread.gt.0.0) call fspread_lorentz(c0,fspread)
        c=sig*c0
        wave(1:NWAVE)=imag(c(0:NWAVE-1))
        peak=maxval(abs(wave))
     endif

     if(snrdb.lt.90) then
        do i=1,NWAVE                   !Add gaussian noise at specified SNR
           xnoise=gran()
           wave(i)=wave(i) + xnoise
        enddo
        do i=NWAVE+1,NZ
           xnoise=gran()
           wave(i)=xnoise
        enddo
     endif

     gain=100.0
     if(nargs.eq.3) then
        gain=10.0**(0.05*level_1)
        if(mod((ifile-1)/10,2).eq.1) gain=10.0**(0.05*level_2)
     endif
     if(snrdb.lt.90.0) then
       wave=gain*wave
     else
       datpk=maxval(abs(wave))
       fac=32766.9/datpk
       wave=fac*wave
     endif
     if(any(abs(wave).gt.32767.0)) print*,"Warning - data will be clipped."
     iwave=nint(wave)

     nDop0=nint(fdop)
     nDopAudio0=0
     nfrit0=0
     f10=f0 + fdop
     fspread0=fspread
     
     h=default_header(12000,NZ)
     h%lenfile=h%lenfile + trailerlen       ! Include trailing metadata in the RIFF size
     n=3*(ifile-1)
     ihr=n/3600
     imin=(n-3600*ihr)/60
     isec=mod(n,60)
     write(fname,1102) ihr,imin,isec
1102 format('000000_',3i2.2,'.wav')
     open(10,file=fname,status='unknown',access='stream')
     write(10) h,iwave                !Save to *.wav file
     call write_wav_info_trailer(10,isft,icrd,icmt)
     close(10)
     write(*,1110) ifile,f0,fdop,fspread,snrdb,fname
1110 format(i4,4f7.1,2x,a17)
  enddo

999 end program echosim

integer*4 function wav_info_field_bytes(nchar)
! Match BWFFile's NUL-terminated, word-aligned INFO subchunk layout.
  integer, intent(in) :: nchar
  integer*4 n
  n=nchar+1
  wav_info_field_bytes=8+n+mod(n,2)
end function wav_info_field_bytes

integer*4 function wav_trailer_length(isft,icrd,icmt)
! Return the complete LIST chunk size, including its 8-byte header.
  character(len=*), intent(in) :: isft,icrd,icmt
  integer*4 wav_info_field_bytes
  wav_trailer_length = 8 + 4                            &  !'LIST' id+size, 'INFO'
       + wav_info_field_bytes(len_trim(isft))            &
       + wav_info_field_bytes(len_trim(icrd))            &
       + wav_info_field_bytes(len_trim(icmt))
end function wav_trailer_length

subroutine write_wav_info_trailer(lu,isft,icrd,icmt)
! Append LIST/INFO metadata at the current stream position.
  integer, intent(in) :: lu
  character(len=*), intent(in) :: isft,icrd,icmt
  integer*4 listlen,wav_info_field_bytes

  listlen = 4                                            &  !'INFO'
       + wav_info_field_bytes(len_trim(isft))             &
       + wav_info_field_bytes(len_trim(icrd))             &
       + wav_info_field_bytes(len_trim(icmt))

  write(lu) 'LIST',listlen,'INFO'
  call write_wav_info_field(lu,'ISFT',isft(1:len_trim(isft)))
  call write_wav_info_field(lu,'ICRD',icrd(1:len_trim(icrd)))
  call write_wav_info_field(lu,'ICMT',icmt(1:len_trim(icmt)))
end subroutine write_wav_info_trailer

subroutine write_wav_info_field(lu,id,value)
! Write one NUL-terminated, word-aligned INFO subchunk.
  integer, intent(in) :: lu
  character*4, intent(in) :: id
  character(len=*), intent(in) :: value
  integer*4 n
  n=len(value)+1
  write(lu) id,n,value,char(0)
  if(mod(n,2).eq.1) write(lu) char(0)
end subroutine write_wav_info_field
