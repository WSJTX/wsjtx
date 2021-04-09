subroutine qra64zap(cx,cy,xpol,nzap)

  parameter (NFFT1=5376000)              !56*96000
  parameter (NFFT2=336000)               !56*6000 (downsampled by 1/16)
  complex cx(0:NFFT2-1),cy(0:NFFT2-1)
  real s(-1312:1312)
  integer iloc(1)
  logical xpol

  slimit=3.0
  sbottom=1.5
  nadd=128
  nblks=NFFT2/nadd
  nbh=nblks/2
  k=-1
  s=0.
  df=nadd*96000.0/NFFT1
  do i=1,nblks
     j=i
     if(j.gt.nblks/2) j=j-nblks
     do n=1,nadd
        k=k+1
        s(j)=s(j) + real(cx(k))**2 + aimag(cx(k))**2
        if(xpol) s(j)=s(j) + real(cy(k))**2 + aimag(cy(k))**2
     enddo
  enddo
  call pctile(s,nblks,45,base)
  s=s/base
  do nzap=1,3
     iloc=maxloc(s)
     ipk=iloc(1)-1313
     smax=s(ipk)
     nw=3
     do n=1,3
        nw=2*nw
        if(ipk-2*nw.lt.-1312) cycle
        if(ipk+2*nw.gt. 1312) cycle
        s1=maxval(s(ipk-2*nw:ipk-nw))
        s2=maxval(s(ipk+nw:ipk+2*nw))
        if(smax.gt.slimit .and. s1.lt.sbottom .and. s2.lt.sbottom) then
           s(ipk-nw:ipk+nw)=1.0
           i0=ipk
           if(i0.lt.0) i0=i0+2625
           ia=(i0-nw)*nadd
           ib=(i0+nw)*nadd
           cx(ia:ib)=0.
           cy(ia:ib)=0.
           exit
        endif
     enddo
  enddo
     
!  rewind 75
!  do i=-nbh,nbh   
!     freq=i*df
!     write(75,3001) freq,s(i)
!3001 format(2f12.3)
!  enddo
!  flush(75)

  return
end subroutine qra64zap
