program test_jtty_get_msgs
  use jtty_mdec, only: nslots,slot
  implicit none

  logical*1 :: all_new,qso_new
  logical*1 :: qso_eom(30),all_eom(30)
  integer :: all_slot_ids(30)
  real :: all_tsync(30),qso_tsync(30)
  real :: f0,ftol
  character(len=2400) :: all_freqs
  character(len=800) :: qso_freq

  nslots=2
  slot(1)%f1=1500.4
  slot(1)%decoded='CQ K1ABC'
  slot(1)%is_last_frame=.false.
  slot(1)%frame_tsync(1)=1.25
  slot(2)%f1=1499.6
  slot(2)%decoded='CQ K1ABC FN20'
  slot(2)%is_last_frame=.true.
  slot(2)%frame_tsync(1)=3.5
  f0=1500.0
  ftol=100.0

  call jtty_get_msgs(f0,ftol,all_new,qso_new,all_freqs,qso_freq,qso_eom, &
       all_tsync,qso_tsync,all_eom,all_slot_ids)

  if(.not.all_new) call fail('initial all-frequency snapshot is new')
  if(all_slot_ids(1).ne.2 .or. all_slot_ids(2).ne.1) &
       call fail('slot identities follow frequency-sorted output')
  if(.not.all_eom(1) .or. all_eom(2)) &
       call fail('completion flags follow their decoder slots')
  if(abs(all_tsync(1)-3.5).gt.0.0001 .or. &
       abs(all_tsync(2)-1.25).gt.0.0001) &
       call fail('start times follow their decoder slots')
  if(index(all_freqs,'1500  CQ K1ABC FN20'//char(10)).eq.0 .or. &
       index(all_freqs,char(10)//'1500  CQ K1ABC'//char(10)).eq.0) &
       call fail('same-prefix decodes remain separate output lines')

  slot(1)%is_last_frame=.true.
  call jtty_get_msgs(f0,ftol,all_new,qso_new,all_freqs,qso_freq,qso_eom, &
       all_tsync,qso_tsync,all_eom,all_slot_ids)

  if(all_new) call fail('unchanged text does not report a new snapshot')
  if(.not.all_eom(2)) &
       call fail('completion metadata updates without a text change')

  print *, 'test_jtty_get_msgs: all checks passed'

contains

  subroutine fail(message)
    character(len=*), intent(in) :: message

    write(*,'(a)') message
    error stop 1
  end subroutine fail

end program test_jtty_get_msgs
