subroutine ft8_a8d(dd,mycall,dxcall,dxgrid,f1a,xdt,fbest,xsnr,plog,msgbest, &
     progress_generation,knowledge,evidence,worker_count)

! List decoding for FT8, activated only at nfqso (Rx Freq) +/- 10 Hz and when
! DxCall and DxGrid are populated. Returns xdt, fbest, and msgbest.

  use packjt77
  use ft8_codec_context, only: get_ft8_codec_state,genft8_for_state
  use decoder_engine_types, only: ft8_signal_evidence,engine_payload_hypothesis
  use ft8_a7
  use timer_module, only: timer
  use decode_completion_module, only: write_decode_progress
  include 'ft8_params.f90'           !Set various constants
  parameter (NWAVE=NN*NSPS/NDOWN)    !Length of generated cwave = 2528
  parameter (NZZ=3200)               !Length of downsampled arrays
  parameter (NFFT=NZZ,NH=NFFT/2)     !FFT length
  parameter (NMSGS=206)              !Default number of message hypotheses
  parameter (PLOG_MIN=-160.0)        !Minimum log probability
  character mycall*12,dxcall*12,dxgrid*6
  character*37 msg,msgsent,msgbest
  character(len=37) :: local_msgbest
  character(len=37), allocatable :: msg_list(:)
  integer*1 msgbits(77)
  integer*1 msgbits_best(77),local_msgbits_best(77)
  integer(kind=1), allocatable :: msgbits_msg(:,:)
  real xjunk(NZZ)
  real s(-NH:NH)                     !Power spectrum of cd0
!  real s00(-NH:NH)                   !Raw spectrum of downsampled data
  real raw_spectrum(-NH:NH)
  real s0(-NH:NH)                    !Best-fit re-centered spectrum for this msg
  real s1(-NH:NH)                    !Overall best-fit re-centered spectrum
  real :: local_s1(-NH:NH),local_sbest,local_fbest,local_tbest
  real s8(0:7)                       !Spectrum of a single symbol
  real dd(NMAX)                      !Floating point copy of iwave
  real a(5)
  complex cwave(0:NZZ-1)             !Complex waveform corresponding to msg
  complex cd(0:NZZ-1)                !Data from dd, now complex and sampled at 200 Hz
  complex cd0(0:NZZ-1)               !All tones in cd nominally shifted to 0 Hz
  complex csymb(0:31)
  integer itone(NN)
  integer itone_best(NN),local_itone_best(NN)
  integer, allocatable :: itone_msg(:,:)
  integer :: nvalid,nworkers,ibest_msg,local_best_index
  integer, allocatable :: msg_index(:)
  integer :: ioffset,i1,i2,peak_mask
  complex, allocatable :: cwave_conj_msg(:,:)
  integer, intent(in) :: progress_generation
  integer, intent(in), optional :: worker_count
  type(pack77_state), target, optional, intent(inout) :: knowledge
  type(pack77_state), pointer :: codec
  type(ft8_signal_evidence), optional, intent(out) :: evidence
  integer ipk(1)
  logical newdat,dstd,mstd,lag_improved
  integer nrun,ndx

  codec => get_ft8_codec_state()
  if(present(knowledge)) codec => knowledge
  if(present(evidence)) evidence=ft8_signal_evidence()
  f1=f1a
  newdat=.true.
! Mix from f1 to baseband and downsample from dd into cd (complex, 200 Hz sampling)
  call ft8_downsample(dd,newdat,f1,cd)
  fac=1.e-6

  fsd=200.0                              !Downsampled rate (Hz)
  bt=2.0                                 !Gaussian smoothing parameter
  dt=1.0/fsd
  df=200.0/NFFT
  ss=0.
  sq=0.

! Find the message that fits the received waveform best
  sbest=0.
  tbest=0.
  fbest=0.
  msgbest=''
  itone_best=0
  msgbits_best=0
! Preserve the nonstandard-call hypotheses for standard /R and /P DX calls.
  nrun=NMSGS
  ndx=len_trim(dxcall)
  call stdcall_suffix(dxcall,dstd)
  if(dstd .and. ndx.gt.3) then
     if(dxcall(ndx-1:ndx).eq.'/R' .or. dxcall(ndx-1:ndx).eq.'/P') then
        nrun=207
        call stdcall_suffix(mycall,mstd)
        if(mstd) nrun=411
     endif
  endif
  nvalid=0
  allocate(msg_list(nrun),msg_index(nrun),msgbits_msg(77,nrun), &
       itone_msg(NN,nrun),cwave_conj_msg(0:NWAVE-1,nrun))
  do imsg=1,nrun
     call write_decode_progress(progress_generation)
     call getmsg(imsg,mycall,dxcall,dxgrid,msg)
     if(len_trim(msg).eq.0) cycle

     call genft8_for_state(codec,msg,i3,n3,msgsent,msgbits,itone)
     call gen_ft8wave(itone,NN,32,bt,fsd,0.0,cwave,xjunk,1,NWAVE)
     nvalid=nvalid+1
     msg_list(nvalid)=msg
     msg_index(nvalid)=imsg
     msgbits_msg(:,nvalid)=msgbits
     itone_msg(:,nvalid)=itone
     cwave_conj_msg(:,nvalid)=conjg(cwave(0:NWAVE-1))
  enddo
  if(nvalid.eq.0) return

  nworkers=1
  if(present(worker_count)) nworkers=max(1,min(worker_count,nvalid))
  ibest_msg=nrun+1
!$omp parallel num_threads(nworkers) if(nworkers.gt.1) default(shared)         &
!$omp& private(imsg,spk,fpk,tpk,lagpk,lag1,lag2,lagstep,iter,lag,i,j,ioffset, &
!$omp& i1,i2,peak_mask,cd0,s,raw_spectrum,s0,smax,lag_improved,local_sbest,local_fbest,local_tbest,local_msgbest, &
!$omp& local_best_index,local_itone_best,local_msgbits_best,local_s1)
  raw_spectrum=0.
  s=0.
  local_sbest=0.
  local_best_index=nrun+1
!$omp do schedule(static)
  do imsg=1,nvalid
     call write_decode_progress(progress_generation)
     spk=0.
     fpk=0.
     tpk=0.
     lagpk=0
     lag1=-200
     lag2=200
     lagstep=4
     do iter=1,2
        call write_decode_progress(progress_generation)
        if(iter.eq.2) then
           lag1=lagpk-8
           lag2=lagpk+8
           lagstep=1
        endif
        do lag=lag1,lag2,lagstep
           ioffset=lag+100
           i1=max(0,-ioffset)
           i2=min(NWAVE-1,NWAVE-1-ioffset)
           cd0=0.
           if(i1.le.i2) cd0(i1:i2)=cd(i1+ioffset:i2+ioffset)*cwave_conj_msg(i1:i2,imsg)
           call four2a(cd0,NFFT,1,-1,1)               !Forward c2c FFT

           do i=0,NFFT-1
              j=i
              if(i.gt.NH) j=i-NFFT
              raw_spectrum(j)=fac*(real(cd0(i))**2 + aimag(cd0(i))**2)
           enddo

           s(-NH)=raw_spectrum(-NH)
           s(NH)=raw_spectrum(NH)
           peak_mask=merge(1,0,(s(-NH).gt.spk).or.(s(NH).gt.spk))
! Use an integer mask so the comparison reduction can remain vectorized.
!$omp simd reduction(ior:peak_mask)
           do j=-NH+1,NH-1
              s(j)=0.5*raw_spectrum(j) + 0.25*(raw_spectrum(j-1)+raw_spectrum(j+1))
              peak_mask=ior(peak_mask,merge(1,0,s(j).gt.spk))
           enddo
           lag_improved=peak_mask.ne.0

           if(lag_improved) then
              lag_improved=.false.
              smax=0.
              do j=-NH,NH
                 smax=max(s(j),smax)
                 if(s(j).gt.spk) then
                    spk=s(j)
                    fpk=j*df+f1
                    lagpk=lag
                    tpk=lag*dt
                    lag_improved=.true.
                 endif
              enddo
           endif
           if(lag_improved) s0=s
        enddo
     enddo

     if(spk.gt.local_sbest) then
        local_sbest=spk
        local_fbest=fpk
        local_tbest=tpk
        local_msgbest=msg_list(imsg)
        local_best_index=msg_index(imsg)
        local_itone_best=itone_msg(:,imsg)
        local_msgbits_best=msgbits_msg(:,imsg)
        local_s1=s0
     endif
  enddo
!$omp end do nowait
!$omp critical(ft8_a8d_best)
  ! Ties select the earliest hypothesis, regardless of worker completion order.
  if(local_sbest.gt.sbest.or. &
       (local_sbest.eq.sbest.and.local_best_index.lt.ibest_msg)) then
     sbest=local_sbest
     fbest=local_fbest
     tbest=local_tbest
     msgbest=local_msgbest
     ibest_msg=local_best_index
     itone_best=local_itone_best
     msgbits_best=local_msgbits_best
     s1=local_s1
  endif
!$omp end critical(ft8_a8d_best)
!$omp end parallel
  deallocate(msg_list,msg_index,msgbits_msg,itone_msg,cwave_conj_msg)

  a=0
  a(1)=f1-fbest
!  if(abs(a(1)+5.0).gt.10.0) then             !Effective Ftol = 10 Hz
  if(abs(a(1)).gt.5.0) then                   !Effective Ftol = 5 Hz
     msgbest=''
     return
  endif
  call twkfreq1(cd,NZZ,200.0,a,cd)           !Re-center the spectrum
  
  xdt=tbest
  ave=(sum(s1(-200:-100)) + sum(s1(100:200)))/202.0
  s1=s1/ave - 1.0
  s1pk=maxval(s1(-32:32))
  sig=0.
  nsig=0
  do i=-32,32
     if(s1(i).lt.0.5*s1pk) cycle
     sig=sig+s1(i)
     nsig=nsig+1
  enddo
  sig=sig/nsig
  xsnr=db(sig)-35.0
  if(xsnr.lt.-30) xsnr=-30                   !Set min SNR for 'a8' decodes

  if(msgbest.ne.'') then
! Compute probability of successful decode.
     plog=0.0
     nhard=0
     nsum=0
     sum_sync=0.
     sum_sig=0.
     sum_big=0.
     do k=1,NN
        i0=32*(k-1) + nint((tbest+0.5)/0.005)
        csymb=0.
        do i=0,31
           if(i0+i.ge.0 .and. i0+i.le.NZZ-1) csymb(i)=cd(i0+i)
        enddo
        call four2a(csymb,32,1,-1,1)           !c2c
        do i=0,7
           s8(i)=real(csymb(i))**2 + aimag(csymb(i))**2
        enddo
        s8sum=sum(s8)
        if(s8sum.gt.0.0) then
           p=s8(itone_best(k))/s8sum
           plog=plog + log(p)
           nsum=nsum+1
        endif
        ipk=maxloc(s8)-1
        if(ipk(1).ne.itone_best(k)) nhard=nhard+1
        if(k.le.7 .or. (k.ge.37 .and. k.le.43) .or. k.ge.73) then
           sum_sync=sum_sync + s8(itone_best(k))
        else
           sum_sig=sum_sig + s8(itone_best(k))
        endif
        sum_big=sum_big + s8(ipk(1))
     enddo
     if(nsum.lt.NN) plog=plog + (NN-nsum)*log(0.125)
     sigobig=(sum_sync+sum_sig)/sum_big
!     write(60,3060) nsum,xdt,fbest,xsnr,plog,nhard,sigosync,sigobig,trim(msgbest)
!3060 format(i2,f8.3,3f8.1,i5,2f7.2,1x,a)
     if(nhard.gt.54 .or. plog.lt.-159.0 .or. sigobig.lt.0.71) msgbest=''
  endif
  if(present(evidence) .and. msgbest.ne.'') then
     evidence%payload77=msgbits_best
     evidence%tones=int(itone_best,kind=1)
     evidence%payload_origin=engine_payload_hypothesis
     evidence%has_tones=1
     evidence%has_waveform_start=1
     evidence%waveform_start_seconds=xdt+0.5
     evidence%method=8
  endif
  
  return
end subroutine ft8_a8d
