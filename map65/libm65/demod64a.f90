subroutine demod64a(s3,nadd,mrsym,mrprob,mr2sym,mr2prob,ntest,nlow)

!  Demodulate the 64-bin spectra for each of 63 symbols in a frame.

!  Parameters
!     nadd     number of spectra already summed
!     mrsym    most reliable symbol value
!     mr2sym   second most likely symbol value
!     mrprob   probability that mrsym was the transmitted value
!     mr2prob  probability that mr2sym was the transmitted value

  implicit real*8 (a-h,o-z)
  real*4 s3(64,63)
  real*8 fs(64)
  integer mrsym(63),mrprob(63),mr2sym(63),mr2prob(63)
  common/mrscom/ mrs(63),mrs2(63)

  afac=1.1 * float(nadd)**0.64
  scale=255.999

!  Compute average spectral value
  sum=0.
  do j=1,63
     do i=1,64
        sum=sum+s3(i,j)
     enddo
  enddo
  ave=sum/(64.*63.)
  i1=1                                      !Silence warning
  i2=1

!  Compute probabilities for most reliable symbol values
  do j=1,63
     s1=-1.e30
     fsum=0.
     psum=0.
     do i=1,64
        x=min(afac*s3(i,j)/ave,50.d0)
        fs(i)=exp(x)
        fsum=fsum+fs(i)
        psum=psum + s3(i,j)
        if(s3(i,j).gt.s1) then
           s1=s3(i,j)
           i1=i                              !Most reliable
        endif
     enddo

     s2=-1.e30
     do i=1,64
        if(i.ne.i1 .and. s3(i,j).gt.s2) then
           s2=s3(i,j)
           i2=i                              !Second most reliable
        endif
     enddo
!     p1=fs(i1)/fsum                          !Normalized probabilities
!     p2=fs(i2)/fsum
     p1=s1/psum
     p2=s2/psum
     mrsym(j)=i1-1
     mr2sym(j)=i2-1
     mrprob(j)=scale*p1
     mr2prob(j)=scale*p2
     mrs(j)=i1
     mrs2(j)=i2
  enddo
  
  sum=0.
  nlow=0
  do j=1,63
     sum=sum+mrprob(j)
     if(mrprob(j).le.5) nlow=nlow+1
  enddo
!  ntest=sum/63
  ntest=sum

  return
end subroutine demod64a
