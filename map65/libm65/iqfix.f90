subroutine iqfix(c,nfft,gain,phase)

  complex c(0:nfft-1)
  complex z,h,u,v

  nh=nfft/2
  h=gain*cmplx(cos(phase),sin(phase))

  do i=1,nh-1
     u=c(i)
     v=c(nfft-i)
     x=real(u)  + real(v)  - (aimag(u) + aimag(v))*aimag(h) +         &
          (real(u) - real(v))*real(h)
     y=aimag(u) - aimag(v) + (aimag(u) + aimag(v))*real(h)  +         &
          (real(u) - real(v))*aimag(h)
     c(i)=0.5*cmplx(x,y)
     z=u
     u=v
     v=z
     x=real(u)  + real(v)  - (aimag(u) + aimag(v))*aimag(h) +         &
          (real(u) - real(v))*real(h)
     y=aimag(u) - aimag(v) + (aimag(u) + aimag(v))*real(h)  +         &
          (real(u) - real(v))*aimag(h)
     c(nfft-i)=0.5*cmplx(x,y)
  enddo

  return
end subroutine iqfix
