module ft8_waveform
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  implicit none
  private
  public :: emit_ft8_waveform
contains
  ! Emit selected samples while advancing phase on the full sampling grid.
  subroutine emit_ft8_waveform(increments,nwave,nsym,nsps,table,ntab,twopi, &
       sample_stride,output_count,cwave,wave)
    integer, intent(in) :: nwave,nsym,nsps,ntab,sample_stride,output_count
    real, intent(in) :: increments(nwave),twopi
    complex, intent(in) :: table(0:ntab-1)
    complex, intent(out), optional :: cwave(output_count)
    real, intent(out), optional :: wave(output_count)
    real :: phi,phase_sum,ramp_up(nint(nsps/8.0)),ramp_down(nint(nsps/8.0))
    integer :: icmplx
    integer :: next_output,output_index,k,i,nramp,k1,first_ramp_last,tail_first,tail_last,tail_end
    logical :: bounded_wrap
    if(present(cwave).eqv.present(wave)) error stop 'Provide exactly one waveform output'
    icmplx=merge(1,0,present(cwave))
    if(sample_stride.lt.1.or.nwave.lt.1) error stop 'Invalid emission stride or full-rate sample count'
    if(output_count.ne.1+(nwave-1)/sample_stride) error stop 'Incorrect emitted sample count'
    ! At most one wrap correction is needed for finite increments within half a turn.
    bounded_wrap=all(ieee_is_finite(increments))
    if(bounded_wrap) bounded_wrap=all(abs(increments).le.0.5*twopi)
    if(icmplx.eq.0) wave=0.
    if(icmplx.ne.0) cwave=0.
    phi=0.
    next_output=1
    output_index=0
    if(sample_stride.eq.1) then
      do k=1,nwave
         if(icmplx.eq.0) then
            wave(k)=sin(phi)
         else
            i=phi*float(ntab)/twopi
            cwave(k)=table(i)
         endif
         if(bounded_wrap) then
            phase_sum=phi+increments(k)
            if(phase_sum.ge.twopi) then
               phi=phase_sum-twopi
            elseif(phase_sum.le.-twopi) then
               phi=phase_sum+twopi
               if(phi.eq.0.) phi=sign(0.,phase_sum)
            else
               phi=phase_sum
            endif
         else
            phi=mod(phi+increments(k),twopi)
         endif
      enddo
    else
      do k=1,nwave
         if(k.eq.next_output) then
            output_index=output_index+1
            if(icmplx.eq.0) then
               wave(output_index)=sin(phi)
            else
               i=phi*float(ntab)/twopi
               cwave(output_index)=table(i)
            endif
            if(output_index.lt.output_count) next_output=next_output+sample_stride
         endif
         if(bounded_wrap) then
            phase_sum=phi+increments(k)
            if(phase_sum.ge.twopi) then
               phi=phase_sum-twopi
            elseif(phase_sum.le.-twopi) then
               phi=phase_sum+twopi
               if(phi.eq.0.) phi=sign(0.,phase_sum)
            else
               phi=phase_sum
            endif
         else
            phi=mod(phi+increments(k),twopi)
         endif
      enddo
    endif
    nramp=nint(nsps/8.0)
    if(nramp.gt.0) then
      ramp_up=(1.0-cos(twopi*(/(i,i=0,nramp-1)/)/(2.0*nramp)))/2.0
      ramp_down=(1.0+cos(twopi*(/(i,i=0,nramp-1)/)/(2.0*nramp)))/2.0
      first_ramp_last=min(output_count,1+(nramp-1)/sample_stride)
      do output_index=1,first_ramp_last
         k=1+(output_index-1)*sample_stride
         if(icmplx.eq.0) then
            wave(output_index)=wave(output_index)*ramp_up(k)
         else
            cwave(output_index)=cwave(output_index)*ramp_up(k)
         endif
      enddo
      tail_end=nsym*nsps
      k1=tail_end-nramp+1
      tail_first=1+(k1-1)/sample_stride
      if((tail_first-1)*sample_stride.lt.k1-1) tail_first=tail_first+1
      tail_last=min(output_count,1+(tail_end-1)/sample_stride)
      do output_index=tail_first,tail_last
         k=1+(output_index-1)*sample_stride
         if(icmplx.eq.0) then
            wave(output_index)=wave(output_index)*ramp_down(k-k1+1)
         else
            cwave(output_index)=cwave(output_index)*ramp_down(k-k1+1)
         endif
      enddo
    endif
  end subroutine emit_ft8_waveform
end module ft8_waveform
