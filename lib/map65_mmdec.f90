subroutine map65_mmdec(nutc,id2,nqd,nsubmode,nfa,nfb,nfqso,ntol,newdat,   &
     nagain,max_drift,mycall,hiscall,hisgrid)

  use prog_args
  use timer_module, only: timer
  use q65_decode

  include 'jt9com.f90'
  include 'timer_common.inc'

  type, extends(q65_decoder) :: counting_q65_decoder
     integer :: decoded
  end type counting_q65_decoder

  logical single_decode,bVHF,lnewdat,lagain,lclearave,lapcqonly
  integer*2 id2(300*12000)
!  type(params_block) :: params
  character(len=20) :: datetime
  character(len=12) :: mycall, hiscall
  character(len=6) :: hisgrid
  data ntr0/-1/
  save
  type(counting_q65_decoder) :: my_q65

! Cast C character arrays to Fortran character strings
!  datetime=transfer(params%datetime, datetime)
!  mycall=transfer(params%mycall,mycall)
!  hiscall=transfer(params%hiscall,hiscall)
!  mygrid=transfer(params%mygrid,mygrid)
!  hisgrid=transfer(params%hisgrid,hisgrid)
  datetime=' '
  
  my_q65%decoded = 0
  ncontest=0
  nQSOprogress=0
  lclearave=.false.
  single_decode=.false.
  lapcqonly=.false.
  lnewdat=(newdat.ne.0)
  lagain=(nagain.ne.0)
  bVHF=.true.
  emedelay=2.5
  ndepth=1
  ntrperiod=60

  open(17,file=trim(temp_dir)//'/red.dat',status='unknown')
  open(14,file=trim(temp_dir)//'/avemsg.txt',status='unknown')

  call timer('dec_q65 ',0)
  call my_q65%decode(q65_decoded,id2,nqd,nutc,ntrperiod,nsubmode,nfqso,       &
       ntol,ndepth,nfa,nfb,lclearave,single_decode,lagain,max_drift,lnewdat,  &
       emedelay,mycall,hiscall,hisgrid,nQSOProgress,ncontest,lapcqonly,navg0)
  call timer('dec_q65 ',1)

  return

contains

  subroutine q65_decoded (this,nutc,snr1,nsnr,dt,freq,decoded,idec,   &
       nused,ntrperiod)

    use q65_decode
    implicit none

    class(q65_decoder), intent(inout) :: this
    integer, intent(in) :: nutc
    real, intent(in) :: snr1
    integer, intent(in) :: nsnr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    integer, intent(in) :: idec
    integer, intent(in) :: nused
    integer, intent(in) :: ntrperiod

    if(nutc+snr1+nsnr+dt+freq+idec+nused+ntrperiod.eq.-999) stop
    if(decoded.eq.'-999') stop

    cq0='q  '
    write(cq0(2:2),'(i1)') idec
    if(nused.ge.2) write(cq0(3:3),'(i1)') nused
    nsnr0=nsnr
    xdt0=dt
    nfreq0=nint(freq)
    msg0=decoded

    select type(this)
    type is (counting_q65_decoder)
       if(idec.ge.0) this%decoded = this%decoded + 1
    end select

   return
 end subroutine q65_decoded
  
end subroutine map65_mmdec
