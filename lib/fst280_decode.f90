module fst280_decode

  type :: fst280_decoder
     procedure(fst280_decode_callback), pointer :: callback
   contains
     procedure :: decode
  end type fst280_decoder

  abstract interface
     subroutine fst280_decode_callback (this,sync,snr,dt,freq,decoded,nap,qual)
       import fst280_decoder
       implicit none
       class(fst280_decoder), intent(inout) :: this
       real, intent(in) :: sync
       integer, intent(in) :: snr
       real, intent(in) :: dt
       real, intent(in) :: freq
       character(len=37), intent(in) :: decoded
       integer, intent(in) :: nap
       real, intent(in) :: qual
     end subroutine fst280_decode_callback
  end interface

contains

  subroutine decode(this,callback,iwave,nQSOProgress,nfqso,    &
       nfa,nfb,ndepth)

    use timer_module, only: timer
    use packjt77
    include 'fst280/fst280_params.f90'
    parameter (MAXCAND=100)
    class(fst280_decoder), intent(inout) :: this
    character*37 msg
    character*120 data_dir
    character*77 c77
    character*1 tr_designator
    complex, allocatable :: c2(:)
    complex, allocatable :: cframe(:)
    complex, allocatable :: c_bigfft(:)          !Complex waveform
    real, allocatable :: r_data(:)
    real*8 fMHz
    real llr(280),llra(280),llrb(280),llrc(280),llrd(280)
    real candidates(100,3)
    real bitmetrics(328,4)
    integer ihdr(11)
    integer*1 apmask(280),cw(280)
    integer*1 hbits(328)
    integer*1 message101(101),message74(74)
    logical badsync,unpk77_success
    integer*2 iwave(300*12000)

    write(*,3001) nQSOProgress,nfqso,nfa,nfb,ndepth
3001 format('aaa',5i5)



  end subroutine decode

end module fst280_decode
