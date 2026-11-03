module waveform_kernels
  use iso_fortran_env, only: real32, real64
  use iso_c_binding, only: c_double, c_float, c_int
  implicit none
  private
  integer, parameter :: block_size=256, table_size=65536
  integer, parameter :: wrap_general=0,wrap_bounded=1,wrap_positive=2
  real(real32), parameter :: twopi32=8.0_real32*atan(1.0_real32)
  real(real64), parameter :: twopi64=8.0_real64*atan(1.0_real64)
  integer :: sine_index
  ! Degree-7 near-minimax fits to H(s)=0.5*erfc(2*pi*sqrt(2/log(2))*s).
  ! Six equal regions cover [0,0.4]; the discarded tail is below 8e-10.
  real(real32), parameter :: gaussian_tail_coefficients(0:7,0:5)=reshape([ &
       5.0000001060e-01_real32, -4.0143699492e-01_real32, &
       2.7879711670e-05_real32, 6.7525139834e-02_real32, &
       8.4390282254e-04_real32, -1.2019631203e-02_real32, &
       1.8832168991e-03_real32, 3.2416026847e-04_real32, &
       1.5714766810e-01_real32, -2.4196063061e-01_real32, &
       1.2248410863e-01_real32, -4.0768555284e-04_real32, &
       -2.0931988665e-02_real32, 4.9998295295e-03_real32, &
       1.3055976697e-03_real32, -5.5327145377e-04_real32, &
       2.2083633794e-02_real32, -5.2982998986e-02_real32, &
       5.3648991606e-02_real32, -2.7283679391e-02_real32, &
       4.7874692750e-03_real32, 2.1822789881e-03_real32, &
       -1.4261730032e-03_real32, 2.5957854055e-04_real32, &
       1.2691003031e-03_real32, -4.2149261689e-03_real32, &
       6.4025699484e-03_real32, -5.7789798419e-03_real32, &
       3.3382097089e-03_real32, -1.2173568110e-03_real32, &
       2.5020123015e-04_real32, -2.0330375958e-05_real32, &
       2.8487585466e-05_real32, -1.2180120633e-04_real32, &
       2.4641871082e-04_real32, -3.1031749180e-04_real32, &
       2.6601979334e-04_real32, -1.5570536506e-04_real32, &
       5.6917427841e-05_real32, -9.7759014033e-06_real32, &
       2.4364111365e-07_real32, -1.2782691538e-06_real32, &
       3.2216653813e-06_real32, -5.1118894247e-06_real32, &
       5.5147612333e-06_real32, -3.9805748439e-06_real32, &
       1.7311866883e-06_real32, -3.3974367042e-07_real32],[8,6])
  complex(real32), parameter :: sine_table(0:table_size-1)=[( &
       cmplx(cos(sine_index*twopi32/table_size),sin(sine_index*twopi32/table_size),real32), &
       sine_index=0,table_size-1)]
  public :: waveform_workspace, render_gfsk, render_fsk, render_tone, apply_waveform_ramp

  type waveform_workspace
     real(real32), allocatable :: pulse(:),scaled_pulse(:)
     integer :: nsps=0,hmod=0
     real(real32) :: bt=0,pulse_bound=0
     logical :: positive_pulse=.true.
  end type

  interface apply_waveform_ramp
     module procedure ramp_real,ramp_real_double,ramp_complex
  end interface
contains
  subroutine prepare_pulse(work,nsps,bt,hmod,constant_pulse)
    type(waveform_workspace), intent(inout) :: work
    integer, intent(in) :: nsps,hmod
    real(real32), intent(in) :: bt
    logical, intent(in) :: constant_pulse
    real(real32) :: t,peak
    logical :: changed
    real(real32), external :: gfsk_pulse
    integer :: i

    if(constant_pulse) then
       if(allocated(work%pulse)) deallocate(work%pulse,work%scaled_pulse)
       work%nsps=0
       work%pulse_bound=abs(twopi32*hmod/real(nsps,real32))
       work%positive_pulse=hmod.ge.0
    else
       changed=work%nsps.ne.nsps.or.work%bt.ne.bt
       if(changed) then
          if(allocated(work%pulse)) deallocate(work%pulse,work%scaled_pulse)
          allocate(work%pulse(3*nsps),work%scaled_pulse(3*nsps))
          do i=1,3*nsps
             t=(i-1.5_real32*nsps)/real(nsps,real32)
             work%pulse(i)=gfsk_pulse(bt,t)
          enddo
          work%nsps=nsps
          work%bt=bt
       endif
       if(changed.or.work%hmod.ne.hmod) then
          peak=twopi32*hmod/real(nsps,real32)
          work%scaled_pulse=peak*work%pulse
          work%hmod=hmod
          work%pulse_bound=maxval(abs(work%scaled_pulse))
          work%positive_pulse=all(work%scaled_pulse.ge.0)
       endif
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

  subroutine prepare_narrow_steps(first,n,nsps,region_scale,next_tone,current_tone,previous_tone,peak,carrier,scale,steps)
    integer, intent(in) :: first,n,nsps,region_scale,next_tone,current_tone,previous_tone
    real(real64), intent(in) :: peak,carrier,scale
    real(real64), intent(out) :: steps(n)
    real(real64) :: inverse_nsps,center,difference,cutoff
    real(real32) :: inverse_nsps32,u,tail,c(0:7)
    integer :: i,j,last,position,region,origin,direction

    inverse_nsps=1.0_real64/nsps
    inverse_nsps32=1.0_real32/nsps
    cutoff=6.0_real64/region_scale
    center=real(current_tone,real64)
    i=1
    do while(i.le.n)
       position=first+i
       if(position.gt.cutoff*nsps.and.position.lt.(1.0_real64-cutoff)*nsps) then
          last=min(n,max(i,ceiling((1.0_real64-cutoff)*nsps)-1-first))
          steps(i:last)=(peak*center+carrier)*scale
       else
          if(position.ge.(1.0_real64-cutoff)*nsps) then
             region=min(5,max(0,int(real(nsps-position,real64)*inverse_nsps*region_scale)))
             last=min(n,max(i,floor(nsps*(1.0_real64-real(region,real64)/region_scale))-first))
             origin=nsps-first
             direction=-1
             difference=real(next_tone,real64)-center
          else
             region=min(5,max(0,int(real(position,real64)*inverse_nsps*region_scale)))
             last=min(n,max(i,ceiling(real(nsps,real64)*(region+1)/region_scale)-1-first))
             origin=first
             direction=1
             difference=real(previous_tone,real64)-center
          endif
          c=gaussian_tail_coefficients(:,region)
          do j=i,last
             ! Form the falling distance as an integer before rounding to float.
             u=real(origin+direction*j,real32)*inverse_nsps32*region_scale-region
             tail=max(0.0_real32,c(0)+u*(c(1)+u*(c(2)+u*(c(3)+u*(c(4)+u*(c(5)+u*(c(6)+u*c(7))))))))
             steps(j)=(peak*(center+difference*real(tail,real64))+carrier)*scale
          enddo
       endif
       i=last+1
    enddo
  end subroutine

  subroutine prepare_bt1_steps(first,n,nsps,next_tone,current_tone,previous_tone,peak,carrier,scale,steps)
    integer, intent(in) :: first,n,nsps,next_tone,current_tone,previous_tone
    real(real64), intent(in) :: peak,carrier,scale
    real(real64), intent(out) :: steps(n)
    real(real64) :: inverse_nsps,center,left_difference,right_difference
    real(real32) :: coordinate_scale,u,v,left_tail,right_tail,left_c(0:7),right_c(0:7)
    integer :: i,j,last,position,left_region,right_region

    inverse_nsps=1.0_real64/nsps
    coordinate_scale=7.5_real32/nsps
    center=real(current_tone,real64)
    left_difference=real(previous_tone,real64)-center
    right_difference=real(next_tone,real64)-center
    left_c=0.0_real32
    i=1
    do while(i.le.n)
       position=first+i
       ! H_BT=1(s)=H_BT=2(s/2); both tails overlap within the symbol.
       left_region=min(6,max(0,int(real(position,real64)*inverse_nsps*7.5_real64)))
       right_region=min(6,max(0,int(real(nsps-position,real64)*inverse_nsps*7.5_real64)))
       last=min(n,max(i,floor(nsps*(1.0_real64-2.0_real64*right_region/15.0_real64))-first))
       if(left_region.lt.6) then
          last=min(last,max(i,ceiling(real(nsps,real64)*2*(left_region+1)/15.0_real64)-1-first))
          left_c=gaussian_tail_coefficients(:,left_region)
       endif
       if(right_region.lt.6) right_c=gaussian_tail_coefficients(:,right_region)
       if(right_region.eq.6) then
          do j=i,last
             u=real(first+j,real32)*coordinate_scale-left_region
             left_tail=max(0.0_real32,left_c(0)+u*(left_c(1)+u*(left_c(2)+u*(left_c(3)+ &
                  u*(left_c(4)+u*(left_c(5)+u*(left_c(6)+u*left_c(7))))))))
             steps(j)=(peak*(center+left_difference*real(left_tail,real64))+carrier)*scale
          enddo
       elseif(left_region.eq.6) then
          do j=i,last
             v=real(nsps-first-j,real32)*coordinate_scale-right_region
             right_tail=max(0.0_real32,right_c(0)+v*(right_c(1)+v*(right_c(2)+v*(right_c(3)+ &
                  v*(right_c(4)+v*(right_c(5)+v*(right_c(6)+v*right_c(7))))))))
             steps(j)=(peak*(center+right_difference*real(right_tail,real64))+carrier)*scale
          enddo
       else
          do j=i,last
             u=real(first+j,real32)*coordinate_scale-left_region
             v=real(nsps-first-j,real32)*coordinate_scale-right_region
             left_tail=max(0.0_real32,left_c(0)+u*(left_c(1)+u*(left_c(2)+u*(left_c(3)+ &
                  u*(left_c(4)+u*(left_c(5)+u*(left_c(6)+u*left_c(7))))))))
             right_tail=max(0.0_real32,right_c(0)+v*(right_c(1)+v*(right_c(2)+v*(right_c(3)+ &
                  v*(right_c(4)+v*(right_c(5)+v*(right_c(6)+v*right_c(7))))))))
             steps(j)=(peak*(center+left_difference*real(left_tail,real64)+ &
                  right_difference*real(right_tail,real64))+carrier)*scale
          enddo
       endif
       i=last+1
    enddo
  end subroutine prepare_bt1_steps

  subroutine render_gfsk(work,tones,nsps,bt,hmod,carrier,source_offset, &
       active_count,sample_stride,repeat_edges,table_output,wave,cwave)
    type(waveform_workspace), intent(inout) :: work
    integer, intent(in) :: tones(:),nsps,hmod,source_offset,active_count,sample_stride
    real(real32), intent(in) :: bt
    real(real64), intent(in) :: carrier
    logical, intent(in) :: repeat_edges,table_output
    real(real32), intent(out), optional :: wave(:)
    complex(real32), intent(out), optional :: cwave(:)
    real(real64) :: phi
    real(real32) :: peak
    real(real64) :: bound,constant_peak
    integer :: wrap_mode
    real(real32) :: increments(block_size)
    real(real64) :: phases(block_size)
    integer :: first,raw,q,r,n,j,p,selected,step,k,last,nsym,i,index
    integer :: p0,p1,p2,t0,t1,t2,region_scale
    logical :: table_blocks,constant_pulse

    if(present(wave).eqv.present(cwave)) error stop 'Provide exactly one waveform output'
    if(sample_stride.lt.1) error stop 'Invalid waveform sample stride'
    constant_pulse=bt.eq.1.0_real32.or.bt.eq.2.0_real32.or.bt.eq.8.0_real32
    region_scale=15
    if(bt.eq.8.0_real32) region_scale=60
    call prepare_pulse(work,nsps,bt,hmod,constant_pulse)
    k=1
    if(active_count.gt.0) k=2+(active_count-1)/sample_stride
    if(present(wave)) wave(k:)=0
    if(present(cwave)) cwave(k:)=0
    nsym=size(tones)
    peak=twopi32*hmod/real(nsps,real32)
    constant_peak=twopi64*real(hmod,real64)/real(nsps,real64)
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
       if(constant_pulse) then
          t0=0; t1=0; t2=0
          if(q.ge.2.and.q.le.nsym+1) t0=tones(q-1)
          if(q.ge.1.and.q.le.nsym) t1=tones(q)
          if(q.ge.0.and.q.lt.nsym) t2=tones(q+1)
          if(repeat_edges) then
             if(q.eq.0) t1=tones(1)
             if(q.eq.1) t0=tones(1)
             if(q.eq.nsym) t2=tones(nsym)
             if(q.eq.nsym+1) t1=tones(nsym)
             if(q.eq.nsym+2) t0=tones(nsym)
          endif
       endif
       if(table_blocks) then
          if(bt.eq.1.0_real32) then
             call prepare_bt1_steps(r,n,nsps,t2,t1,t0,constant_peak,carrier,table_size/twopi64,phases(1:n))
          elseif(constant_pulse) then
             call prepare_narrow_steps(r,n,nsps,region_scale,t2,t1,t0,constant_peak, &
                  carrier,table_size/twopi64,phases(1:n))
          else
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
          endif
          k=first+1
          if(present(wave)) then
             do i=1,n
                index=iand(int(phi),table_size-1)
                wave(k+i-1)=aimag(sine_table(index))
                phi=phi+phases(i)
             enddo
          else
             do i=1,n
                index=iand(int(phi),table_size-1)
                cwave(k+i-1)=sine_table(index)
                phi=phi+phases(i)
             enddo
          endif
          ! Bounded blocks keep table indices in range while reducing double phase only once.
          phi=phi-real(table_size,real64)*aint(phi/real(table_size,real64))
          first=first+n
          cycle
       endif
       if(bt.eq.1.0_real32) then
          call prepare_bt1_steps(r,n,nsps,t2,t1,t0,constant_peak,0.0_real64,1.0_real64,phases(1:n))
       elseif(constant_pulse) then
          call prepare_narrow_steps(r,n,nsps,region_scale,t2,t1,t0,constant_peak, &
               0.0_real64,1.0_real64,phases(1:n))
       else
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
          phases(1:n)=real(increments(1:n),real64)
       endif
       if(table_output.and.sample_stride.eq.1.and.wrap_mode.eq.wrap_positive) then
          k=first+1
          if(present(wave)) then
             do i=1,n
                index=iand(int(phi*(table_size/twopi64)),table_size-1)
                wave(k+i-1)=aimag(sine_table(index))
                phi=phi+(phases(i)+carrier)
                if(phi.ge.twopi64) phi=phi-twopi64
             enddo
          else
             do i=1,n
                index=iand(int(phi*(table_size/twopi64)),table_size-1)
                cwave(k+i-1)=sine_table(index)
                phi=phi+(phases(i)+carrier)
                if(phi.ge.twopi64) phi=phi-twopi64
             enddo
          endif
          first=first+n
          cycle
       endif
       phases(1:n)=phases(1:n)+carrier
       call integrate_phase(phases(1:n),phi,wrap_mode)
       selected=modulo(-first,sample_stride)
       if(selected.lt.n) then
          step=1+(n-1-selected)/sample_stride
          k=1+(first+selected)/sample_stride
          last=selected+1+(step-1)*sample_stride
          if(present(wave)) then
             if(table_output) then
                call emit_table_real(phases(selected+1:last:sample_stride),sine_table,wave(k:k+step-1))
             else
                call emit_real(phases(selected+1:last:sample_stride),wave(k:k+step-1))
             endif
          else
             if(table_output) then
                call emit_table_complex(phases(selected+1:last:sample_stride),sine_table,cwave(k:k+step-1))
             else
                call emit_complex(phases(selected+1:last:sample_stride),cwave(k:k+step-1))
             endif
          endif
       endif
       first=first+n
    enddo
  end subroutine

  pure real(real64) function advance_tone_phase(phase,increment) result(next_phase)
    real(real64), intent(in) :: phase,increment
    next_phase=phase+increment
    if(next_phase.ge.twopi64) then
       next_phase=next_phase-twopi64
    elseif(next_phase.le.-twopi64) then
       next_phase=next_phase+twopi64
    endif
  end function advance_tone_phase

  ! Emit the first sample of each stride and retain phase after every source step.
  subroutine render_tone(phase,phase_step,wave,sample_stride)
    real(real64), intent(inout) :: phase
    real(real64), intent(in) :: phase_step
    real(real32), intent(out) :: wave(:)
    integer, intent(in), optional :: sample_stride
    real(real64) :: phases(block_size),increment
    integer :: first,n,i,j,stride

    stride=1
    if(present(sample_stride)) stride=sample_stride
    if(stride.lt.1) error stop 'Invalid tone emission stride'
    if(size(wave).eq.0) return
    if(abs(phase).ge.twopi64) phase=mod(phase,twopi64)
    increment=phase_step
    if(abs(increment).ge.twopi64) increment=mod(increment,twopi64)
    first=1
    do while(first.le.size(wave))
       n=min(block_size,size(wave)-first+1)
       if(stride.eq.1) then
          do i=1,n
             phase=advance_tone_phase(phase,increment)
             phases(i)=phase
          enddo
       elseif(stride.eq.4) then
          do i=1,n
             phase=advance_tone_phase(phase,increment)
             phases(i)=phase
             phase=advance_tone_phase(phase,increment)
             phase=advance_tone_phase(phase,increment)
             phase=advance_tone_phase(phase,increment)
          enddo
       else
          do i=1,n
             phase=advance_tone_phase(phase,increment)
             phases(i)=phase
             do j=2,stride
                phase=advance_tone_phase(phase,increment)
             enddo
          enddo
       endif
       call emit_real(phases(1:n),wave(first:first+n-1))
       first=first+n
    enddo
  end subroutine render_tone

  subroutine wsjt_render_tone(phase,phase_step,n,sample_stride,wave) bind(C,name='wsjt_render_tone')
    real(c_double), intent(inout) :: phase
    real(c_double), value :: phase_step
    integer(c_int), value :: n,sample_stride
    real(c_float), intent(out) :: wave(n)

    call render_tone(phase,phase_step,wave,sample_stride)
  end subroutine wsjt_render_tone

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

  subroutine ramp_real_double(wave,stride,start,length,denominator,direction)
    real(real32), intent(inout) :: wave(:)
    integer, intent(in) :: stride,start,length
    real(real64), intent(in) :: denominator,direction
    integer :: k,i
    do k=1+(start+stride-1)/stride,min(size(wave),1+(start+length-1)/stride)
       i=(k-1)*stride-start
       wave(k)=wave(k)*(1+direction*cos(twopi64*i/denominator))/2
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
