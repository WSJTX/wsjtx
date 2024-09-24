subroutine qpc_decode2(c0,fsync,ftol,xdec,ndepth,dth,damp,crc_ok,   &
     snrsync,fbest,tbest,snr)

   use qpc_mod

   parameter(NMAX=15*12000,NFT=365,NZ=100)
   complex c0(NMAX)                    !Signal as received
   complex c(NMAX)                     !Signal as received
   real py(0:127,0:127)                !Probabilities for received synbol values
   real py0(0:127,0:127)               !Probabilities for strong signal
   real pyd(0:127,0:127)               !Dithered values for py
   real s2(0:127,0:151)                !Symbol spectra, including sync
   real s3(0:127,0:127)                !Synchronized symbol spectra
   real No
   integer crc_chk,crc_sent
   integer*8 n47
   integer idf(NZ),idt(NZ)
   integer nseed(33)
   integer*1 xdec(0:49)                !Decoded message
   integer*1 ydec(0:127)               !Decoded symbols
   logical crc_ok
   integer maxdither(8)
   integer isync(24)                   !Symbol numbers for sync tones
   data isync/1,2,4,7,11,16,22,29,37,39,42,43,45,48,52,57,63,70,78,80,83,  &
      84,86,89/
   data n47/47/,maxdither/20,50,100,200,500,1000,2000,5000/
   data nseed/                                                         &
      321278106,  -658879006,  1239150429,  -941466001, -698554454, &
      1136210962,  1633585627,  1261915021, -1134191465, -487888229, &
      2131958895, -1429290834, -1802468092,  1801346659, 1966248904, &
      402671397, -1961400750, -1567227835,  1895670987, -286583128, &
      -595933665, -1699285543,  1518291336,  1338407128,  838354404, &
      -2081343776, -1449416716,  1236537391,  -133197638,  337355509, &
      -460640480,  1592689606,          0/

   data idf/0,  0, -1,  0, -1,  1,  0, -1,  1, -2,  0, -1,  1, -2,  2, &
        0, -1,  1, -2,  2, -3,  0, -1,  1, -2,  2, -3,  3,  0, -1, &
        1, -2,  2, -3,  3, -4,  0, -1,  1, -2,  2, -3,  3, -4,  4, &
        0, -1,  1, -2,  2, -3,  3, -4,  4, -5, -1,  1, -2,  2, -3, &
        3, -4,  4, -5,  1, -2,  2, -3,  3, -4,  4, -5, -2,  2, -3, &
        3, -4,  4, -5,  2, -3,  3, -4,  4, -5, -3,  3, -4,  4, -5, &
        3, -4,  4, -5, -4,  4, -5,  4, -5, -5/
   data idt/0 , -1,  0,  1, -1,  0, -2,  1, -1,  0,  2, -2,  1, -1,  0, &
        -3,  2, -2,  1, -1,  0,  3, -3,  2, -2,  1, -1,  0, -4,  3, &
        -3,  2, -2,  1, -1,  0,  4, -4,  3, -3,  2, -2,  1, -1,  0, &
        -5,  4, -4,  3, -3,  2, -2,  1, -1,  0, -5,  4, -4,  3, -3, &
         2, -2,  1, -1, -5,  4, -4,  3, -3,  2, -2,  1, -5,  4, -4, &
         3, -3,  2, -2, -5,  4, -4,  3, -3,  2, -5,  4, -4,  3, -3, &
        -5,  4, -4,  3, -5,  4, -4, -5,  4, -5/


   fsample=12000.0
   baud=12000.0/1024.0
   nstype=1
   n47=47
   mask21=2**21 - 1
   crc_ok=.false.

   call qpc_sync(c0,fsample,isync,fsync,ftol,f2,t2,snrsync)
   f00=1500.0 + f2
   t00=t2
   fbest=f00
   tbest=t00
   maxd=1
   if(ndepth.gt.0) maxd=maxdither(ndepth)
   maxft=NZ
   if(snrsync.lt.4.0 .or. ndepth.le.0) maxft=1
   do idith=1,maxft
      if(idith.ge.2) maxd=1
      deltaf=idf(idith)*0.5
      deltat=idt(idith)*8.0/1024.0
      f=f00+deltaf
      t=t00+deltat
      fshift=1500.0 - (f+baud)        !Shift frequencies down by f + 1 bin
      call twkfreq2(c0,c,NMAX,fsample,fshift)
      a=1.0
      b=0.0
      do kk=1,4
         if(kk.eq.2) b=0.4
         if(kk.eq.3) b=0.5
         if(kk.eq.4) b=0.6
         call sfox_demod(c,1500.0,t,isync,s2,s3)       !Compute s2 and s3

         if(b.gt.0.0) then
            do j=0,127
               call smo121a(s3(:,j),128,a,b)
            enddo
         endif
         call pctile(s3,128*128,50,base3)
         s3=s3/base3

         EsNoDec=3.16
         No=1.
         py0=s3
         call qpc_likelihoods2(py,s3,EsNoDec,No)       !For weak signals
         
         call random_seed(put=nseed)
         do kkk=1,maxd
            if(kkk.eq.1) then
               pyd=py0
            else
               pyd=0.
               if(kkk.gt.2) then
                  call random_number(pyd)
                  pyd=2.0*(pyd-0.5)
               endif
               where(py.gt.dth) pyd=0.          !Don't perturb large likelihoods
               pyd=py*(1.0 + damp*pyd)          !Compute dithered likelihood
            endif
            do j=0,127
               ss=sum(pyd(:,j))
               if(ss.gt.0.0) then
                 pyd(:,j)=pyd(:,j)/ss
               else
                 pyd(:,j)=0.0
               endif
            enddo

            call qpc_decode(xdec,ydec,pyd)
            xdec=xdec(49:0:-1)
            crc_chk=iand(nhash2(xdec,n47,571),mask21)           !Compute crc_chk
            crc_sent=128*128*xdec(47) + 128*xdec(48) + xdec(49)
            crc_ok=crc_chk.eq.crc_sent

            if(crc_ok) then
               call qpc_snr(s3,ydec,snr)
               if(snr.lt.-16.5) crc_ok=.false.
!               write(61,3061) idith,kk,kkk,idf(idith),idt(idith),a,b
!3061           format(5i5,2f8.3)
               return
            endif
         enddo    !kk: dither of smoothing weights
      enddo       !kkk: dither of probabilities
   enddo          !idith: dither of frequency and time
   return
end subroutine qpc_decode2

subroutine smo121a(x,nz,a,b)

  real x(nz)
  fac=1.0/(a+2*b)
  x0=x(1)
  do i=2,nz-1
     x1=x(i)
     x(i)=fac*(a*x(i) + b*(x0+x(i+1)))
     x0=x1
  enddo

  return
end subroutine smo121a
