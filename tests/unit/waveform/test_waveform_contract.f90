program test_waveform_contract
  use iso_fortran_env, only: real32, real64
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use waveform_kernels, only: render_tone
  use ft8_waveform_generator, only: gen_ft8wave_samples
  use ft8var_waveform_generator, only: gen_ft8wavevar_samples
  use fst4_wavegen, only: fst4_wavegen_workspace,gen_fst4wave_owned
  implicit none

  real(real32), parameter :: pi2s=8.0_real32*atan(1.0_real32)
  real(real64), parameter :: pi2d=8.0_real64*atan(1.0_real64)
  real(real32), parameter :: sample_tolerance=1.0e-6_real32
  real(real64), parameter :: table_phase_tolerance=1.0e-6_real64
  real(real32), parameter :: guard=12345.0_real32
  integer, parameter :: ntab=65536
  complex(real32) :: table(0:ntab-1)
  integer :: i
  real(real32) :: phase

  do i=0,ntab-1
     phase=i*pi2s/ntab
     table(i)=cmplx(cos(phase),sin(phase),kind=real32)
  enddo

  call check_ft8(1920,12000.0_real32,2.0_real32,1500.0_real32)
  call check_ft8(1920,16000.0_real32,2.0_real32,1500.0_real32)
  call check_ft8(7680,48000.0_real32,2.0_real32,1500.0_real32)
  call check_ft8(1920,12000.0_real32,1.0_real32,-500.0_real32)
  call check_ft8(1920,12000.0_real32,1.5_real32,0.0_real32)
  call check_ft8(263,12000.0_real32,1.5_real32,123456.75_real32)
  call check_ft8_carrier()
  call check_ft4(576,12000.0_real32,1500.0_real32)
  call check_ft4(2304,48000.0_real32,-500.0_real32)
  call check_ft4(576,12000.0_real32,0.0_real32)
  call check_ft4(263,12000.0_real32,-98765.375_real32)
  call check_fst4(720,12000.0_real32,1500.0_real32,1)
  call check_fst4(2880,48000.0_real32,1500.0_real32,1)
  call check_fst4(720,12000.0_real32,1500.0_real32,0)
  call check_fst4(255,12000.0_real32,5999.5_real32,0)
  call check_fst4(256,12000.0_real32,5999.5_real32,0)
  call check_fst4(257,12000.0_real32,5999.5_real32,0)
  call check_fst4(720,16000.0_real32,1500.0_real32,1)
  call check_fst4(2880,48000.0_real32,-500.0_real32,2)
  call check_fst4(720,12000.0_real32,0.0_real32,1)
  call check_fst4(720,16000.0_real32,123456.75_real32,0)
  call check_owned_workspaces()
  call check_genwave(263,12000.0_real32,1500.0_real32)
  call check_genwave(263,12000.0_real32,123456.75_real32)
  call check_genwave(200003,48000.0_real32,-500.0_real32)
  call check_genwave(65537,48000.0_real32,48000.0_real32/65537)
  call check_genwave(65537,48000.0_real32,-48000.0_real32/65537)
  call check_tone()
  call check_tone_stride()
  print '(a)', 'Waveform contract tests passed'

contains
  subroutine check_tone()
    integer, parameter :: count=4099
    integer, parameter :: chunks(*)=[1,255,256,257,17]
    real(real64), parameter :: starts(*)=[0.0_real64,0.7_real64,-8.3_real64,9*pi2d]
    real(real64), parameter :: steps(*)=[0.0_real64,0.161604_real64,-0.65_real64, &
         pi2d/4,pi2d*3.1_real64,-pi2d*2.3_real64]
    real(real32) :: whole(count),split(count),other(count),interleaved(count),empty(0)
    real(real64) :: phase,split_phase,other_phase,reference_phase,expected,error
    integer :: i,j,k,first,last,chunk

    do i=1,size(starts)
       do j=1,size(steps)
          phase=starts(i)
          call render_tone(phase,steps(j),empty)
          if(phase.ne.starts(i)) error stop 'Empty tone render changed phase'
          call render_tone(phase,steps(j),whole)
          do k=1,count
             expected=sin(starts(i)+k*steps(j))
             error=abs(real(whole(k),real64)-expected)
             if(error.gt.sample_tolerance) call fail('tone sine',k,real(error,real32))
          enddo

          other_phase=-0.23_real64
          call render_tone(other_phase,-0.37_real64,other)
          reference_phase=other_phase
          other_phase=-0.23_real64
          split_phase=starts(i)
          first=1
          chunk=1
          do while(first.le.count)
             last=min(count,first+chunks(chunk)-1)
             call render_tone(split_phase,steps(j),split(first:last))
             call render_tone(other_phase,-0.37_real64,interleaved(first:last))
             first=last+1
             chunk=mod(chunk,size(chunks))+1
          enddo
          if(any(whole.ne.split).or.phase.ne.split_phase) error stop 'Tone depends on block boundaries'
          if(any(other.ne.interleaved).or.other_phase.ne.reference_phase) error stop 'Tone states interfere'
       enddo
    enddo
  end subroutine check_tone

  subroutine check_tone_stride()
    integer, parameter :: count=4099,strides(*)=[1,2,4,7],chunks(*)=[1,255,256,257,17]
    real(real64), parameter :: starts(*)=[0.0_real64,0.7_real64,-8.3_real64,9*pi2d]
    real(real64), parameter :: steps(*)=[0.0_real64,0.161604_real64,-0.65_real64, &
         pi2d/4,pi2d*3.1_real64,-pi2d*2.3_real64]
    real(real32) :: full(count*7),sampled(count),split(count),empty(0)
    real(real64) :: phase,sampled_phase,split_phase
    integer :: i,j,k,stride,first,last,chunk,n

    do i=1,size(starts)
       do j=1,size(steps)
          do k=1,size(strides)
             stride=strides(k)
             n=count*stride
             phase=starts(i)
             call render_tone(phase,steps(j),full(1:n))
             sampled_phase=starts(i)
             call render_tone(sampled_phase,steps(j),empty,stride)
             if(sampled_phase.ne.starts(i)) error stop 'Empty strided render changed phase'
             call render_tone(sampled_phase,steps(j),sampled,stride)
             if(any(sampled.ne.full(1:n:stride)).or.sampled_phase.ne.phase) &
                  error stop 'Strided tone differs from source samples'
             split_phase=starts(i)
             first=1
             chunk=1
             do while(first.le.count)
                last=min(count,first+chunks(chunk)-1)
                call render_tone(split_phase,steps(j),split(first:last),stride)
                first=last+1
                chunk=mod(chunk,size(chunks))+1
             enddo
             if(any(sampled.ne.split).or.split_phase.ne.sampled_phase) &
                  error stop 'Strided tone depends on block boundaries'
          enddo
       enddo
    enddo
  end subroutine check_tone_stride

  real(real32) function pulse_value(bt,t) result(value)
    real(real32), intent(in) :: bt,t
    real(real32) :: c
    c=(4.0_real32*atan(1.0_real32))*sqrt(2.0_real32/log(2.0_real32))
    value=0.5_real32*(erf(c*bt*(t+0.5_real32))-erf(c*bt*(t-0.5_real32)))
  end function pulse_value

  subroutine increments(tones,nsym,nsps,bt,rate,freq,edges,fst,hmod,dphi)
    integer, intent(in) :: nsym,nsps,tones(nsym),hmod
    real(real32), intent(in) :: bt,rate,freq
    logical, intent(in) :: edges,fst
    real(real64), intent(out) :: dphi(0:(nsym+2)*nsps-1)
    real(real32) :: pulse(3*nsps),peak,modulation(0:(nsym+2)*nsps-1)
    real(real64) :: carrier
    integer :: i,j
    do i=1,3*nsps
       pulse(i)=pulse_value(bt,(i-1.5_real32*nsps)/real(nsps,real32))
    enddo
    peak=pi2s*real(hmod,real32)/real(nsps,real32)
    modulation=0.0_real32
    do j=1,nsym
       do i=1,3*nsps
          modulation((j-1)*nsps+i-1)=modulation((j-1)*nsps+i-1)+peak*pulse(i)*tones(j)
       enddo
    enddo
    if(edges) then
       do i=1,2*nsps
          modulation(i-1)=modulation(i-1)+peak*tones(1)*pulse(nsps+i)
          modulation(nsym*nsps+i-1)=modulation(nsym*nsps+i-1)+peak*tones(nsym)*pulse(i)
       enddo
    endif
    if(fst) then
       carrier=pi2d*(real(freq,real64)-1.5_real64*hmod/ &
            (real(nsps,real64)/real(rate,real64)))/real(rate,real64)
    else
       carrier=pi2d*real(freq,real64)/real(rate,real64)
    endif
    dphi=real(modulation,real64)+carrier
  end subroutine increments

  subroutine fail(label,index,error)
    character(len=*), intent(in) :: label
    integer, intent(in) :: index
    real(real32), intent(in) :: error
    print '(a,a,a,i0,a,es14.6)', 'FAIL ',label,' at sample ',index,': ',error
    error stop 1
  end subroutine fail

  subroutine compare(label,actual,expected,phase,table_output,index,adjacent)
    character(len=*), intent(in) :: label
    complex(real32), intent(in) :: actual,expected
    real(real64), intent(in) :: phase
    logical, intent(in) :: table_output
    integer, intent(in) :: index
    complex(real32), intent(in), optional :: adjacent(2)
    real(real32) :: err
    real(real64) :: scaled
    err=abs(actual-expected)
    if(.not.ieee_is_finite(err)) call fail(label,index,err)
    if(err<=sample_tolerance) return
    if(table_output.and.present(adjacent)) then
       scaled=phase*real(ntab,real64)/pi2d
       ! Sub-microradian phase differences can cross a quantized lookup boundary.
       if(abs(scaled-real(nint(scaled),real64))*pi2d/ntab<=table_phase_tolerance.and.err<1.1e-4_real32) then
          if(any(abs(actual-adjacent)<=sample_tolerance)) return
       endif
    endif
    call fail(label,index,err)
  end subroutine compare

  subroutine guards(label,w,c,n)
    character(len=*), intent(in) :: label
    integer, intent(in) :: n
    real(real32), intent(in) :: w(:)
    complex(real32), intent(in) :: c(:)
    if(w(1)/=guard.or.w(n+2)/=guard.or.c(1)/=cmplx(guard,guard).or. &
         c(n+2)/=cmplx(guard,guard)) call fail(label,0,1.0_real32)
  end subroutine guards

  subroutine check_ft8(nsps,rate,bt,freq)
    integer, parameter :: nsym=5
    integer, intent(in) :: nsps
    real(real32), intent(in) :: rate,bt,freq
    integer :: tones(nsym),n,k,i,idx,nramp,stride,count,variant,kind
    real(real64), allocatable :: d(:)
    real(real32), allocatable :: w(:),stridewave(:)
    complex(real32), allocatable :: c(:),stridecomplex(:)
    real(real32) :: env,x
    real(real64) :: phi
    complex(real32) :: expected,adjacent(2)
    tones=[0,7,2,5,1]
    n=nsym*nsps
    allocate(d(0:(nsym+2)*nsps-1),w(n+2),c(n+2))
    call increments(tones,nsym,nsps,bt,rate,freq,.true.,.false.,1,d)
    nramp=nint(nsps/8.0_real32)
    do variant=1,2
       do kind=0,1
          w=guard
          c=cmplx(guard,guard)
          if(variant==1) then
             call gen_ft8wave(tones,nsym,nsps,bt,rate,freq,c(2),w(2),kind,n)
          else
             call gen_ft8wavevar(tones,nsym,nsps,bt,rate,freq,c(2),w(2),kind,n)
          endif
          call guards('FT8 guards',w,c,n)
          phi=0.0_real64
          do k=1,n
             env=1.0_real32
             if(k<=nramp) env=(1.0_real32-cos(pi2s*(k-1)/(2.0_real32*nramp)))/2.0_real32
             if(k>n-nramp) env=(1.0_real32+cos(pi2s*(k-(n-nramp+1))/(2.0_real32*nramp)))/2.0_real32
             if(kind==0) then
                x=sin(real(phi,real32))*env
                call compare('FT8 real',cmplx(w(k+1),0.0_real32),cmplx(x,0.0_real32),phi,.false.,k)
             else
                idx=iand(int(phi*real(ntab,real64)/pi2d),ntab-1)
                expected=table(idx)*env
                adjacent=table([iand(idx-1,ntab-1),iand(idx+1,ntab-1)])*env
                call compare('FT8 complex',c(k+1),expected,phi,.true.,k,adjacent)
             endif
             phi=mod(phi+real(d(nsps+k-1),real64),pi2d)
          enddo
       enddo
       stride=4
       count=1+(n-1)/stride
       allocate(stridewave(count),stridecomplex(count))
       if(variant==1) then
          call gen_ft8wave_samples(tones,nsym,nsps,bt,rate,freq,n,stride,count,wave=stridewave)
          call gen_ft8wave_samples(tones,nsym,nsps,bt,rate,freq,n,stride,count,cwave=stridecomplex)
       else
          call gen_ft8wavevar_samples(tones,nsym,nsps,bt,rate,freq,n,stride,count,wave=stridewave)
          call gen_ft8wavevar_samples(tones,nsym,nsps,bt,rate,freq,n,stride,count,cwave=stridecomplex)
       endif
       ! Compare against independently accumulated reference by reusing full-rate calls.
       if(variant==1) then
          call gen_ft8wave(tones,nsym,nsps,bt,rate,freq,c(2),w(2),0,n)
       else
          call gen_ft8wavevar(tones,nsym,nsps,bt,rate,freq,c(2),w(2),0,n)
       endif
       do i=1,count
          k=1+(i-1)*stride
          if(abs(stridewave(i)-w(k+1))>1.0e-6_real32) call fail('FT8 stride real',i,abs(stridewave(i)-w(k+1)))
          if(abs(stridecomplex(i)-c(k+1))>1.0e-6_real32) &
               call fail('FT8 stride complex',i,abs(stridecomplex(i)-c(k+1)))
       enddo
       deallocate(stridewave,stridecomplex)
    enddo
  end subroutine check_ft8

  subroutine check_ft8_carrier()
    integer, parameter :: nsym=79,nsps=1920,n=nsym*nsps
    integer :: tones(nsym),k
    real(real32) :: rate,freq,bt,error
    real(real32), allocatable :: wave(:)
    complex(real32) :: unused(1)
    real(real64) :: phi,step
    tones=0
    rate=12000.0_real32
    freq=1500.0_real32
    bt=2.0_real32
    allocate(wave(n))
    call gen_ft8wave(tones,nsym,nsps,bt,rate,freq,unused,wave,0,n)
    step=pi2d*real(freq,real64)/real(rate,real64)
    do k=nsps/8+1,n-nsps/8
       phi=mod(real(k-1,real64)*step,pi2d)
       error=abs(wave(k)-sin(real(phi,real32)))
       if(error>1.0e-6_real32.or..not.ieee_is_finite(error)) call fail('FT8 carrier',k,error)
    enddo
  end subroutine check_ft8_carrier

  subroutine check_ft4(nsps,rate,freq)
    integer, parameter :: nsym=5
    integer, intent(in) :: nsps
    real(real32), intent(in) :: rate,freq
    integer :: tones(nsym),n,k,kind
    real(real64), allocatable :: d(:)
    real(real32), allocatable :: w(:)
    complex(real32), allocatable :: c(:)
    real(real32) :: env,p
    real(real64) :: phi
    complex(real32) :: expected
    tones=[0,3,1,2,0]
    n=(nsym+2)*nsps
    allocate(d(0:n-1),w(n+2),c(n+2))
    call increments(tones,nsym,nsps,1.0_real32,rate,freq,.false.,.false.,1,d)
    do kind=0,1
       w=guard
       c=cmplx(guard,guard)
       call gen_ft4wave(tones,nsym,nsps,rate,freq,c(2),w(2),kind,n)
       call guards('FT4 guards',w,c,n)
       phi=0.0_real64
       do k=1,n
          env=1.0_real32
          if(k<=nsps) env=(1.0_real32-cos(pi2s*(k-1)/(2.0_real32*nsps)))/2.0_real32
          if(k>(nsym+1)*nsps) env=(1.0_real32+cos(pi2s*(k-(nsym+1)*nsps-1)/(2.0_real32*nsps)))/2.0_real32
          p=real(phi,real32)
          if(kind==0) then
             expected=cmplx(sin(p)*env,0.0_real32)
             call compare('FT4 real',cmplx(w(k+1),0.0_real32),expected,phi,.false.,k)
          else
             expected=cmplx(cos(p),sin(p))*env
             call compare('FT4 complex',c(k+1),expected,phi,.false.,k)
          endif
          phi=mod(phi+real(d(k-1),real64),pi2d)
       enddo
    enddo
  end subroutine check_ft4

  subroutine check_fst4(nsps,rate,freq,hmod)
    integer, parameter :: nsym=5
    integer, intent(in) :: nsps,hmod
    real(real32), intent(in) :: rate,freq
    integer :: tones(nsym),n,k,kind,idx,nlen,start
    real(real64), allocatable :: d(:)
    real(real32), allocatable :: w(:)
    complex(real32), allocatable :: c(:)
    real(real32) :: env
    real(real64) :: phi
    character(len=1) :: no_shape
    logical :: shape
    complex(real32) :: expected,adjacent(2)
    tones=[0,3,1,2,0]
    n=nsym*nsps+2
    allocate(d(0:(nsym+2)*nsps-1),w(n+2),c(n+2))
    call increments(tones,nsym,nsps,2.0_real32,rate,freq,.false.,.true.,hmod,d)
    call get_environment_variable('FST4_NOSHAPING',no_shape,nlen)
    shape=.not.(nlen==1.and.no_shape=='1')
    start=(nsym-1)*nsps+3*nsps/4+1
    do kind=0,1
       w=guard
       c=cmplx(guard,guard)
       call gen_fst4wave(tones,nsym,nsps,n,rate,hmod,freq,kind,c(2),w(2))
       call guards('FST4 guards',w,c,n)
       phi=0.0_real64
       do k=1,nsym*nsps
          env=1.0_real32
          if(shape.and.k<=nsps/4) env=(1.0_real32-cos(pi2s*(k-1)/real(nsps/2,real32)))/2.0_real32
          if(shape.and.k>=start) env=(1.0_real32+cos(pi2s*(k-start)/real(nsps/2,real32)))/2.0_real32
          idx=iand(int(phi*real(ntab,real64)/pi2d),ntab-1)
          expected=table(idx)*env
          adjacent=table([iand(idx-1,ntab-1),iand(idx+1,ntab-1)])*env
          if(kind==0) then
             call compare('FST4 real',cmplx(w(k+1),0.0_real32),cmplx(aimag(expected),0.0_real32), &
                  phi,.true.,k,cmplx(aimag(adjacent),0.0_real32))
          else
             call compare('FST4 complex',c(k+1),expected,phi,.true.,k,adjacent)
          endif
          phi=mod(phi+real(d(nsps+k-1),real64),pi2d)
       enddo
       if(kind==0) then
          if(any(w(nsym*nsps+2:n+1)/=0.0_real32)) call fail('FST4 real padding',0,1.0_real32)
       else
          if(any(c(nsym*nsps+2:n+1)/=cmplx(0.0_real32,0.0_real32))) call fail('FST4 complex padding',0,1.0_real32)
       endif
    enddo
  end subroutine check_fst4

  subroutine check_owned_workspaces()
    integer, parameter :: nsym=5,nsps=720,n=nsym*nsps+2
    integer :: tones(nsym)
    real(real32) :: a(n),b(n),again(n)
    complex(real32) :: unused(1)
    type(fst4_wavegen_workspace) :: work_a,work_b
    tones=[0,3,1,2,0]
    call gen_fst4wave_owned(work_a,tones,nsym,nsps,n,12000.0_real32,1,1500.0_real32,0,unused,a)
    call gen_fst4wave_owned(work_b,tones,nsym,nsps,n,16000.0_real32,2,-500.0_real32,0,unused,b)
    call gen_fst4wave_owned(work_a,tones,nsym,nsps,n,12000.0_real32,1,1500.0_real32,0,unused,again)
    if(any(a/=again)) call fail('FST4 independent workspace A',0,maxval(abs(a-again)))
    call gen_fst4wave_owned(work_b,tones,nsym,nsps,n,16000.0_real32,2,-500.0_real32,0,unused,again)
    if(any(b/=again)) call fail('FST4 independent workspace B',0,maxval(abs(b-again)))
  end subroutine check_owned_workspaces

  subroutine check_genwave(nsps,rate,freq)
    integer, parameter :: nsym=3
    integer, intent(in) :: nsps
    real(real32), intent(in) :: rate,freq
    integer :: tones(nsym),n,k,j,kind
    real(real32), allocatable :: w(:)
    complex(real32), allocatable :: c(:)
    real(real64) :: phi,step,spacing
    complex(real32) :: expected
    tones=[0,3,1]
    spacing=6.25_real64
    n=nsym*nsps
    allocate(w(n+2),c(n+2))
    do kind=0,1
       w=guard
       c=cmplx(guard,guard)
       call genwave(tones,nsym,nsps,n,rate,spacing,freq,kind,c(2),w(2))
       call guards('genwave guards',w,c,n)
       phi=0.0_real64
       do j=1,nsym
          step=pi2d*(real(freq,real64)+tones(j)*spacing)/real(rate,real64)
          do k=1,nsps
             if(kind==0) then
                expected=cmplx(sin(real(phi,real32)),0.0_real32)
                call compare('genwave real',cmplx(w((j-1)*nsps+k+1),0.0_real32),expected,phi,.false.,k)
             else
                expected=cmplx(cos(real(phi,real32)),sin(real(phi,real32)))
                call compare('genwave complex',c((j-1)*nsps+k+1),expected,phi,.false.,k)
             endif
             phi=mod(phi+step,pi2d)
          enddo
       enddo
    enddo
  end subroutine check_genwave
end program test_waveform_contract
