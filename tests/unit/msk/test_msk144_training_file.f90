! SPDX-License-Identifier: GPL-3.0-or-later
! MSK144 training on ideal frames: msk144signalquality is fed the very model it
! fits against, so five frames 0.1 s apart complete a training. Its file goes
! in the directory given as the argument; one it cannot create is skipped.
program test_msk144_training_file
  use msk144_training, only: trainings_done, trained_file, trained_call, trained_terms
  implicit none
  character(len=500) :: dir
  character(len=512) :: first_file
  logical*1 :: btrain
  logical :: exists, same_second
  integer :: nfail, attempt, size_before, size_after
  real :: t0
  character(len=14) :: before, after

  nfail = 0
  t0 = 0.0
  call get_command_argument(1, dir)
  if (len_trim(dir) .eq. 0) stop 'usage: test_msk144_training_file <directory>'

  btrain = .true.
  call frames('K1ABC W9XYZ R-04', 'W9XYZ', 4)
  call ok('four frames: training goes on', btrain .and. trainings_done .eq. 0)
  call frames('K1ABC W9XYZ R-04', 'W9XYZ', 1)
  inquire(file=trained_file, exist=exists)
  call ok('the fifth completes it: train cleared, the file written in the directory', &
       .not. btrain .and. trainings_done .eq. 1 .and. exists .and.                      &
       trained_file(1:len_trim(dir) + 7) .eq. trim(dir) // '/W9XYZ_' .and.             &
       trained_call .eq. 'W9XYZ')
  call ok('the coefficients are the file''s header', all(trained_terms .eq. header(trained_file)))
  call remove(trained_file)

  ! A call with '/' names a subdirectory that does not exist.
  btrain = .true.
  call frames('K1ABC W9XYZ/R R-04', 'W9XYZ/R', 5)
  inquire(file=trim(dir) // '/W9XYZ', exist=exists)
  call ok('a call with / completes training with no file and no abort',              &
       .not. btrain .and. trainings_done .eq. 2 .and. trained_file .eq. ' ' .and.      &
       .not. exists .and. trained_call .eq. 'W9XYZ/R' .and. all(abs(trained_terms) .lt. huge(1.d0)))
  ! It leaves the state a success leaves: no training under way, and a new one needs five frames.
  call frames('K1ABC W9XYZ/R R-04', 'W9XYZ/R', 5)
  call ok('after it, frames with train off train nothing', trainings_done .eq. 2)
  btrain = .true.
  call frames('K1ABC W9XYZ/R R-04', 'W9XYZ/R', 4)
  call ok('and a re-armed training needs five frames again', btrain .and. trainings_done .eq. 2)
  call frames('K1ABC W9XYZ/R R-04', 'W9XYZ/R', 1)

  ! A second training of the call within the same second finds its file there.
  same_second = .false.
  do attempt = 1, 5
     call now(before)
     btrain = .true.
     call frames('K1ABC W9XYZ R-04', 'W9XYZ', 5)
     first_file = trained_file
     inquire(file=first_file, size=size_before)
     btrain = .true.
     call frames('K1ABC W9XYZ R-04', 'W9XYZ', 5)
     call now(after)
     inquire(file=first_file, size=size_after)
     same_second = before .eq. after
     if (same_second) exit
     call remove(trained_file)
     call remove(first_file)
  end do
  call ok('a re-arm completing in the same second: no file, no abort, its coefficients sent', &
       same_second .and. .not. btrain .and. trained_file .eq. ' ' .and. first_file .ne. ' ' .and. &
       size_after .eq. size_before .and. all(abs(trained_terms) .lt. huge(1.d0)))
  call remove(first_file)

  if (nfail .eq. 0) then
     print '(a)', 'ALL MSK144 TRAINING FILE CHECKS PASSED'
  else
     print '(a,i0,a)', 'MSK144 TRAINING FILE: ', nfail, ' CHECK(S) FAILED'
     call exit(1)
  end if

contains

  ! n ideal frames of msg 0.1 s apart, as decodes for dxcall.
  subroutine frames(msg, dxcall, n)
    character(len=*), intent(in) :: msg, dxcall
    integer,          intent(in) :: n
    character(len=37) :: message, msgsent
    character(len=12) :: call12
    integer :: i4tone(144), msgbits(144), itype, i, j, idx, nbiterrors
    real :: softbits(144), waveform(0:863), d(1024), phi, dphi, twopi, eyeopening
    complex :: canalytic(1024), cmodel(1024), cframe(864)
    real(8) :: pcoeffs(5)
    message = msg
    call12 = dxcall
    pcoeffs = 0.d0
    call genmsk_128_90(message, 0, msgsent, i4tone, itype)
    msgbits(1) = 0
    do i = 1, 143
       if ((i4tone(i) .eq. 0) .eqv. (mod(i, 2) .eq. 1)) then
          msgbits(i+1) = msgbits(i)
       else
          msgbits(i+1) = mod(msgbits(i) + 1, 2)
       end if
    end do
    softbits = 2.0 * msgbits - 1.0
    twopi = 8.0 * atan(1.0)
    phi = -twopi / 8
    idx = 0
    do i = 1, 144
       dphi = twopi * (1500.0 + merge(-500.0, 500.0, i4tone(i) .eq. 0)) / 12000.0
       do j = 1, 6
          waveform(idx) = cos(phi)
          idx = idx + 1
          phi = mod(phi + dphi, twopi)
       end do
    end do
    d = 0
    d(1:864) = waveform
    call analytic(d, 864, 1024, canalytic, pcoeffs, .false._1)
    call tweak1(canalytic, 1024, -1500.0, cmodel)
    do i = 1, n
       t0 = t0 + 0.1
       cframe = cmodel(1:864)
       call msk144signalquality(cframe, 10.0, 1500.0, t0, softbits, message, call12, &
            btrain, trim(dir), nbiterrors, eyeopening, pcoeffs)
    end do
  end subroutine frames

  ! The five coefficients of a .pcoeff file's header.
  function header(file) result(terms)
    character(len=*), intent(in) :: file
    real(8) :: terms(5)
    integer :: n, freq_low, freq_high, nterms, unit
    real :: chi, rmsdiff
    open(newunit=unit, file=file, status='old', action='read')
    read(unit, '(i4,2f10.2,3i5,5e25.16)') n, chi, rmsdiff, freq_low, freq_high, nterms, terms
    close(unit)
  end function header

  subroutine remove(file)
    character(len=*), intent(in) :: file
    integer :: unit, ios
    if (file .eq. ' ') return
    open(newunit=unit, file=file, status='old', iostat=ios)
    if (ios .eq. 0) close(unit, status='delete')
  end subroutine remove

  ! The local time to the second, as the training file's name has it.
  subroutine now(stamp)
    character(len=14), intent(out) :: stamp
    integer :: values(8)
    call date_and_time(values=values)
    write(stamp, '(i4.4,5i2.2)') values(1), values(2), values(3), values(5), values(6), values(7)
  end subroutine now

  subroutine ok(label, cond)
    character(len=*), intent(in) :: label
    logical,          intent(in) :: cond
    if (cond) then
       print '(a,a)', '  PASS  ', label
    else
       print '(a,a)', '  FAIL  ', label
       nfail = nfail + 1
    end if
  end subroutine ok

end program test_msk144_training_file
