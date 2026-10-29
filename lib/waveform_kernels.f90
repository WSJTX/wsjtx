module waveform_kernels
  use iso_fortran_env, only: real32, real64
  implicit none
  private
  integer, parameter :: block_size=256, table_size=65536
  integer, parameter :: wrap_general=0,wrap_bounded=1,wrap_positive=2
  real(real32), parameter :: twopi32=8.0_real32*atan(1.0_real32)
  real(real64), parameter :: twopi64=8.0_real64*atan(1.0_real64)
  public :: waveform_workspace, render_gfsk, render_fsk, apply_waveform_ramp

  type waveform_workspace
     real(real32), allocatable :: pulse(:),scaled_pulse(:)
     complex(real32), allocatable :: table(:)
     integer :: nsps=0,hmod=0
     real(real32) :: rate=0,bt=0,pulse_bound=0
     logical :: positive_pulse=.true.
  end type

  interface apply_waveform_ramp
     module procedure ramp_real,ramp_complex
  end interface
contains
  subroutine prepare_pulse(work,nsps,rate,bt,hmod,table_output)
    type(waveform_workspace), intent(inout) :: work
    integer, intent(in) :: nsps,hmod
    real(real32), intent(in) :: rate,bt
    logical, intent(in) :: table_output
    real(real32) :: t,peak,phi
    logical :: changed
    real(real32), external :: gfsk_pulse
    integer :: i

    changed=work%nsps.ne.nsps.or.work%rate.ne.rate.or.work%bt.ne.bt
    if(changed) then
       if(allocated(work%pulse)) deallocate(work%pulse,work%scaled_pulse)
       allocate(work%pulse(3*nsps),work%scaled_pulse(3*nsps))
       do i=1,3*nsps
          t=(i-1.5_real32*nsps)/real(nsps,real32)
          work%pulse(i)=gfsk_pulse(bt,t)
       enddo
       work%nsps=nsps
       work%rate=rate
       work%bt=bt
    endif
    if(changed.or.work%hmod.ne.hmod) then
       peak=twopi32*hmod/real(nsps,real32)
       work%scaled_pulse=peak*work%pulse
       work%hmod=hmod
       work%pulse_bound=maxval(abs(work%scaled_pulse))
       work%positive_pulse=all(work%scaled_pulse.ge.0)
    endif
    if(table_output.and..not.allocated(work%table)) then
       allocate(work%table(0:table_size-1))
       do i=0,table_size-1
          phi=i*twopi32/table_size
          work%table(i)=cmplx(cos(phi),sin(phi),real32)
       enddo
    endif
  end subroutine

  subroutine integrate_phase(phases,phi,wrap_mode)
    real(real64), intent(inout) :: phases(:),phi
    real(real64) :: increment,s
    integer, intent(in) :: wrap_mode
    integer :: i

    if(wrap_mode.eq.wrap_positive) then
       do i=1,size(phases)
          increment=phases(i)
          phases(i)=phi
          phi=phi+increment
          if(phi.ge.twopi64) phi=phi-twopi64
       enddo
       return
    endif
    if(wrap_mode.eq.wrap_bounded) then
       do i=1,size(phases)
          increment=phases(i)
          phases(i)=phi
          s=phi+increment
          if(s.ge.twopi64) then
             phi=s-twopi64
          elseif(s.le.-twopi64) then
             phi=s+twopi64
          else
             phi=s
          endif
       enddo
    else
       do i=1,size(phases)
          increment=phases(i)
          phases(i)=phi
          phi=mod(phi+increment,twopi64)
       enddo
    endif
  end subroutine

  ! Odd degree-9 minimax sine on [-pi/2,pi/2], with unit linear coefficient.
  subroutine emit_real(phases,wave)
    real(real64), intent(in) :: phases(:)
    real(real32), intent(out) :: wave(:)
    real(real32), parameter :: pi=3.14159265358979323846_real32, inverse_pi=1.0_real32/pi
    real(real32), parameter :: tau_lo=-1.748455600074495e-7_real32
    real(real32), parameter :: c1=-1.66666567325592041e-01_real32
    real(real32), parameter :: c2=8.33301711827516556e-03_real32
    real(real32), parameter :: c3=-1.98066147277131677e-04_real32
    real(real32), parameter :: c4=2.60005481322878040e-06_real32
    real(real32) :: x,x2,cycle
    integer :: i
    do i=1,size(wave)
       x=real(phases(i),real32)
       cycle=sign(real(min(1,int(abs(x)*inverse_pi)),real32),x)
       x=(x-cycle*twopi32)-cycle*tau_lo
       x=sign(min(abs(x),pi-abs(x)),x)
       x2=x*x
       wave(i)=x+(x*x2)*(c1+x2*(c2+x2*(c3+x2*(c4))))
    enddo
  end subroutine

  subroutine emit_complex(phases,wave)
    real(real64), intent(in) :: phases(:)
    complex(real32), intent(out) :: wave(:)
    real(real32) :: x
    integer :: i
    do i=1,size(wave)
       x=real(phases(i),real32)
       wave(i)%re=cos(x)
    enddo
    do i=1,size(wave)
       x=real(phases(i),real32)
       wave(i)%im=sin(x)
    enddo
  end subroutine

  subroutine emit_table_real(phases,table,wave)
    real(real64), intent(in) :: phases(:)
    complex(real32), intent(in) :: table(0:)
    real(real32), intent(out) :: wave(:)
    integer :: i,index
    do i=1,size(wave)
       index=iand(int(phases(i)*(table_size/twopi64)),table_size-1)
       wave(i)=aimag(table(index))
    enddo
  end subroutine

  subroutine emit_table_complex(phases,table,wave)
    real(real64), intent(in) :: phases(:)
    complex(real32), intent(in) :: table(0:)
    complex(real32), intent(out) :: wave(:)
    integer :: i,index
    do i=1,size(wave)
       index=iand(int(phases(i)*(table_size/twopi64)),table_size-1)
       wave(i)=table(index)
    enddo
  end subroutine

  subroutine prepare_table_steps(n,p0,p1,p2,t0,t1,t2,carrier,steps)
    integer, intent(in) :: n,t0,t1,t2
    real(real32), intent(in) :: p0(n),p1(n),p2(n)
    real(real64), intent(in) :: carrier
    real(real64), intent(out) :: steps(n)
    real(real32) :: modulation
    integer :: i
    do i=1,n
       modulation=0.0_real32+p0(i)*t0
       modulation=modulation+p1(i)*t1
       modulation=modulation+p2(i)*t2
       steps(i)=(real(modulation,real64)+carrier)*(table_size/twopi64)
    enddo
  end subroutine

  subroutine render_gfsk(work,tones,nsps,rate,bt,hmod,carrier,source_offset, &
       active_count,sample_stride,repeat_edges,table_output,wave,cwave)
    type(waveform_workspace), intent(inout) :: work
    integer, intent(in) :: tones(:),nsps,hmod,source_offset,active_count,sample_stride
    real(real32), intent(in) :: rate,bt
    real(real64), intent(in) :: carrier
    logical, intent(in) :: repeat_edges,table_output
    real(real32), intent(out), optional :: wave(:)
    complex(real32), intent(out), optional :: cwave(:)
    real(real64) :: phi
    real(real32) :: peak
    real(real64) :: bound
    integer :: wrap_mode
    real(real32) :: increments(block_size)
    real(real64) :: phases(block_size)
    integer :: first,raw,q,r,n,j,p,selected,step,k,last,nsym,i,index
    integer :: p0,p1,p2,t0,t1,t2
    logical :: table_blocks

    if(present(wave).eqv.present(cwave)) error stop 'Provide exactly one waveform output'
    if(sample_stride.lt.1) error stop 'Invalid waveform sample stride'
    call prepare_pulse(work,nsps,rate,bt,hmod,table_output)
    k=1
    if(active_count.gt.0) k=2+(active_count-1)/sample_stride
    if(present(wave)) wave(k:)=0
    if(present(cwave)) cwave(k:)=0
    nsym=size(tones)
    peak=twopi32*hmod/real(nsps,real32)
    bound=abs(real(carrier,real64))+3.0_real64*work%pulse_bound*maxval(abs(real(tones,real64)))
    wrap_mode=wrap_general
    if(bound.le.0.5_real64*twopi64) wrap_mode=wrap_bounded
    if(wrap_mode.eq.wrap_bounded.and.carrier.ge.0.and.minval(tones).ge.0.and.work%positive_pulse) wrap_mode=wrap_positive
    table_blocks=table_output.and.sample_stride.eq.1.and.wrap_mode.eq.wrap_positive.and..not.repeat_edges
    phi=0
    first=0
    do while(first.lt.active_count)
       raw=source_offset+first
       q=raw/nsps
       r=mod(raw,nsps)
       n=min(block_size,nsps-r,active_count-first)
       if(table_blocks) then
          j=max(0,q-2)
          p0=1
          t0=0
          if(j.le.min(nsym-1,q)) then
             p0=(q-j)*nsps+r+1
             t0=tones(j+1)
          endif
          p1=p0
          p2=p0
          t1=0
          t2=0
          if(j+1.le.min(nsym-1,q)) then
             p1=p0-nsps
             t1=tones(j+2)
          endif
          if(j+2.le.min(nsym-1,q)) then
             p2=p0-2*nsps
             t2=tones(j+3)
          endif
          call prepare_table_steps(n,work%scaled_pulse(p0:p0+n-1),work%scaled_pulse(p1:p1+n-1), &
               work%scaled_pulse(p2:p2+n-1),t0,t1,t2,carrier,phases(1:n))
          k=first+1
          if(present(wave)) then
             do i=1,n
                index=iand(int(phi),table_size-1)
                wave(k+i-1)=aimag(work%table(index))
                phi=phi+phases(i)
             enddo
          else
             do i=1,n
                index=iand(int(phi),table_size-1)
                cwave(k+i-1)=work%table(index)
                phi=phi+phases(i)
             enddo
          endif
          ! Bounded blocks keep table indices in range while reducing double phase only once.
          phi=phi-real(table_size,real64)*aint(phi/real(table_size,real64))
          first=first+n
          cycle
       endif
       increments(1:n)=0
       do j=max(0,q-2),min(nsym-1,q)
          p=(q-j)*nsps+r+1
          increments(1:n)=increments(1:n)+work%scaled_pulse(p:p+n-1)*tones(j+1)
       enddo
       if(repeat_edges) then
          ! FT8's virtual edge symbols are added after the message pulses.
          if(q.le.1) then
             p=(q+1)*nsps+r+1
             increments(1:n)=increments(1:n)+peak*tones(1)*work%pulse(p:p+n-1)
          endif
          if(q.ge.nsym.and.q.le.nsym+2) then
             p=(q-nsym)*nsps+r+1
             increments(1:n)=increments(1:n)+peak*tones(nsym)*work%pulse(p:p+n-1)
          endif
       endif
       if(table_output.and.sample_stride.eq.1.and.wrap_mode.eq.wrap_positive) then
          k=first+1
          if(present(wave)) then
             do i=1,n
                index=iand(int(phi*(table_size/twopi64)),table_size-1)
                wave(k+i-1)=aimag(work%table(index))
                phi=phi+(real(increments(i),real64)+carrier)
                if(phi.ge.twopi64) phi=phi-twopi64
             enddo
          else
             do i=1,n
                index=iand(int(phi*(table_size/twopi64)),table_size-1)
                cwave(k+i-1)=work%table(index)
                phi=phi+(real(increments(i),real64)+carrier)
                if(phi.ge.twopi64) phi=phi-twopi64
             enddo
          endif
          first=first+n
          cycle
       endif
       phases(1:n)=real(increments(1:n),real64)+carrier
       call integrate_phase(phases(1:n),phi,wrap_mode)
       selected=modulo(-first,sample_stride)
       if(selected.lt.n) then
          step=1+(n-1-selected)/sample_stride
          k=1+(first+selected)/sample_stride
          last=selected+1+(step-1)*sample_stride
          if(present(wave)) then
             if(table_output) then
                call emit_table_real(phases(selected+1:last:sample_stride),work%table,wave(k:k+step-1))
             else
                call emit_real(phases(selected+1:last:sample_stride),wave(k:k+step-1))
             endif
          else
             if(table_output) then
                call emit_table_complex(phases(selected+1:last:sample_stride),work%table,cwave(k:k+step-1))
             else
                call emit_complex(phases(selected+1:last:sample_stride),cwave(k:k+step-1))
             endif
          endif
       endif
       first=first+n
    enddo
  end subroutine

  subroutine render_fsk(tones,nsps,rate,spacing,carrier,wave,cwave)
    integer, intent(in) :: tones(:),nsps
    real(real64), intent(in) :: rate,spacing,carrier
    real(real32), intent(out), optional :: wave(:)
    complex(real32), intent(out), optional :: cwave(:)
    real(real64) :: phases(block_size),phi,increment
    integer :: symbol,first,n,k,wrap_mode
    if(present(wave).eqv.present(cwave)) error stop 'Provide exactly one waveform output'
    if(present(wave)) wave=0
    if(present(cwave)) cwave=0
    phi=0
    k=1
    do symbol=1,size(tones)
       increment=twopi64*(carrier+tones(symbol)*spacing)/rate
       first=0
       do while(first.lt.nsps)
          n=min(block_size,nsps-first)
          wrap_mode=wrap_general
          if(abs(increment).le.0.5_real64*twopi64) wrap_mode=wrap_bounded
          if(wrap_mode.eq.wrap_bounded.and.increment.ge.0.and.phi.ge.0) wrap_mode=wrap_positive
          phases(1:n)=increment
          call integrate_phase(phases(1:n),phi,wrap_mode)
          if(present(wave)) then
             call emit_real(phases(1:n),wave(k:k+n-1))
          else
             call emit_complex(phases(1:n),cwave(k:k+n-1))
          endif
          first=first+n
          k=k+n
       enddo
    enddo
  end subroutine

  subroutine ramp_real(wave,stride,start,length,denominator,direction)
    real(real32), intent(inout) :: wave(:)
    integer, intent(in) :: stride,start,length
    real(real32), intent(in) :: denominator,direction
    integer :: k,i
    do k=1+(start+stride-1)/stride,min(size(wave),1+(start+length-1)/stride)
       i=(k-1)*stride-start
       wave(k)=wave(k)*(1+direction*cos(twopi32*i/denominator))/2
    enddo
  end subroutine

  subroutine ramp_complex(wave,stride,start,length,denominator,direction)
    complex(real32), intent(inout) :: wave(:)
    integer, intent(in) :: stride,start,length
    real(real32), intent(in) :: denominator,direction
    integer :: k,i
    do k=1+(start+stride-1)/stride,min(size(wave),1+(start+length-1)/stride)
       i=(k-1)*stride-start
       wave(k)=wave(k)*(1+direction*cos(twopi32*i/denominator))/2
    enddo
  end subroutine
end module
