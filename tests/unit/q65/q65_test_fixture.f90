module q65_test_fixture

  use iso_fortran_env, only: int16
  implicit none

  integer, parameter :: q65_ntrperiod=60
  integer, parameter :: q65_nsamples=300*12000
  integer, parameter :: q65_nsymbols=85
  integer, parameter :: q65_tones(q65_nsymbols)=[ &
       0,3,28,56,36,21,7,6,0,10,56,0,0,1,0,41,26,35,54,33, &
       4,0,0,24,9,0,0,42,42,8,12,9,0,60,0,51,61,0,48,22, &
       22,10,46,60,20,0,5,3,4,0,11,41,23,5,0,54,47,56,1,0, &
       1,0,29,26,29,0,33,1,0,32,41,31,31,0,29,0,50,28,38,24, &
       33,30,41,10,0]

contains

  subroutine make_q65_wave(iwave,nsubmode)
    integer(int16), intent(out) :: iwave(:)
    integer, intent(in) :: nsubmode
    integer, parameter :: complex_rate=6000
    integer, parameter :: samples_per_symbol=3600
    integer, parameter :: leading_silence=complex_rate
    real, parameter :: pi=3.14159265358979323846
    real, parameter :: amplitude=28000.0
    real :: phase,frequency,tone_spacing
    integer :: symbol,sample,complex_sample

    iwave=0_int16
    phase=0.0
    tone_spacing=12000.0/7200.0*real(2**nsubmode)
    do symbol=1,q65_nsymbols
       frequency=1000.0+real(q65_tones(symbol))*tone_spacing
       do sample=0,samples_per_symbol-1
          complex_sample=leading_silence+(symbol-1)*samples_per_symbol+sample
          if(2*complex_sample+2.gt.size(iwave)) return
          phase=phase+2.0*pi*frequency/real(complex_rate)
          iwave(2*complex_sample+1)=int(nint(amplitude*cos(phase)),int16)
          iwave(2*complex_sample+2)=int(nint(-amplitude*sin(phase)),int16)
       enddo
    enddo
  end subroutine make_q65_wave

end module q65_test_fixture
