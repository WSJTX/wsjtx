! SPDX-License-Identifier: GPL-3.0-or-later
! The GUI's MSK144 receive path: hspec as MainWindow::fastSink calls it, once
! per 3584-sample block of each period, with the decoder's state kept for the
! whole script as the GUI process keeps it.
!
! usage: msk144_gui_model <script>
! Script lines, in order:
!   audio <file>            the session's 16-bit little-endian samples
!   datadir <dir>
!   mycall [call]           hiscall [call]     (no call: blank)
!   ntr|depth|rxfreq|ntol <n>
!   shmsg|swl|train 0|1
!   pcoeffs <c1> <c2> <c3> <c4> <c5>
!   period <nutc> <first> <count>   samples [first, first+count) as one period
! Output: "decode <period> <block> |<line>", "trained <period> <block> [|<file>]"
! when the block's call completed a training (with the coefficient file it
! wrote), "end <period>"; block n ends at sample 3584*n of its period.
program msk144_gui_model
  implicit none
  integer, parameter :: BLOCK_SAMPLES = 3584
  character(len=1024) :: script, buffer, rest, audio_file
  character(len=1024) :: datadir
  character(len=32) :: keyword
  character(len=12) :: mycall, hiscall
  character(len=80) :: line
  integer*2, allocatable :: audio(:), id2(:)
  real :: green(0:702), spectrum(0:63,0:702), pxmax, dbnogain
  real*8 :: pcoeffs(5)
  logical*1 :: bmsk144, btrain, bshmsg, bswl
  logical :: armed, wrote
  integer :: unit, ios, blank, value, ntr, ndepth, nrxfreq, ntol, ingain, jh
  integer :: nutc, first, ncount, iperiod, n, k, ntrpdepth, nul, nsamples
  integer :: before(8), after(8), nwritten
  character(len=1024) :: written(16)

  if (command_argument_count() .ne. 1) stop 2
  call get_command_argument(1, script)
  open(newunit=unit, file=trim(script), status='old', action='read', iostat=ios)
  if (ios .ne. 0) stop 3

  allocate(id2(0:120*12000-1))
  datadir = '.'
  mycall = ' '
  hiscall = ' '
  ntr = 15
  ndepth = 3
  nrxfreq = 1500
  ntol = 50
  bmsk144 = .true.
  btrain = .false.
  bshmsg = .false.
  bswl = .false.
  pcoeffs = 0.d0
  ! MainWindow::m_inGain is never changed from 0.
  ingain = 0
  green = 0.
  spectrum = 0.
  jh = 0
  iperiod = 0
  nsamples = 0
  nwritten = 0

  do
     read(unit, '(a)', iostat=ios) buffer
     if (ios .ne. 0) exit
     if (len_trim(buffer) .eq. 0) cycle
     blank = index(buffer, ' ')
     keyword = buffer(1:blank-1)
     rest = adjustl(buffer(blank:))
     select case (trim(keyword))
     case ('audio')
        audio_file = rest
        call load_audio()
     case ('datadir')
        datadir = rest
     case ('mycall')
        mycall = rest(1:12)
     case ('hiscall')
        hiscall = rest(1:12)
     case ('ntr')
        read(rest, *) ntr
     case ('depth')
        read(rest, *) ndepth
     case ('rxfreq')
        read(rest, *) nrxfreq
     case ('ntol')
        read(rest, *) ntol
     case ('shmsg')
        read(rest, *) value
        bshmsg = value .ne. 0
     case ('swl')
        read(rest, *) value
        bswl = value .ne. 0
     case ('train')
        read(rest, *) value
        btrain = value .ne. 0
     case ('pcoeffs')
        read(rest, *) pcoeffs
     case ('period')
        read(rest, *) nutc, first, ncount
        call run_period()
     case default
        stop 4
     end select
  end do
  close(unit)

contains

  subroutine load_audio()
    integer :: u, bytes
    open(newunit=u, file=trim(audio_file), access='stream', form='unformatted', &
         status='old', action='read', iostat=ios)
    if (ios .ne. 0) stop 5
    inquire(unit=u, size=bytes)
    nsamples = bytes / 2
    if (allocated(audio)) deallocate(audio)
    allocate(audio(max(nsamples, 1)))
    if (nsamples .gt. 0) read(u) audio(1:nsamples)
    close(u)
  end subroutine load_audio

  ! The GUI's period buffer starts zeroed (ReceiveAudio.hpp), and each block
  ! lands in it before fastSink sees the block's end.
  subroutine run_period()
    if (first .lt. 0 .or. ncount .lt. 0 .or. first + ncount .gt. nsamples) stop 6
    iperiod = iperiod + 1
    id2 = 0
    ntrpdepth = ntr + 1000 * iand(ndepth, 3)
    do n = 1, ncount / BLOCK_SAMPLES
       k = BLOCK_SAMPLES * n
       id2(k-BLOCK_SAMPLES:k-1) = audio(first+k-BLOCK_SAMPLES+1:first+k)
       line = ' '
       line(1:1) = char(0)
       armed = btrain
       call date_and_time(values=before)
       call hspec(id2, k, nutc, ntrpdepth, nrxfreq, ntol, bmsk144, btrain, pcoeffs, &
            ingain, mycall, hiscall, bshmsg, bswl, trim(datadir), green, spectrum,  &
            jh, pxmax, dbnogain, line)
       call date_and_time(values=after)
       wrote = new_coefficient_file()
       nul = index(line, char(0))
       if (nul .gt. 1) write(*, '(a,i0,a,i0,2a)') 'decode ', iperiod, ' ', n, ' |', line(1:nul-1)
       ! A training that goes on after the toggle is cleared shows only in its file.
       if (wrote) then
          write(*, '(a,i0,a,i0,2a)') 'trained ', iperiod, ' ', n, ' |', trim(written(nwritten))
       else if (armed .and. .not. btrain) then
          write(*, '(a,i0,a,i0)') 'trained ', iperiod, ' ', n
       end if
    end do
    write(*, '(a,i0)') 'end ', iperiod
    flush(6)
  end subroutine run_period

  ! msk144signalquality names the file from date_and_time when it writes it:
  ! look for that name at every second the call spanned.
  logical function new_coefficient_file()
    integer :: day, second, last
    character(len=13) :: stamp
    character(len=1024) :: name
    logical :: exists
    new_coefficient_file = .false.
    do day = 1, merge(1, 2, all(before(1:3) .eq. after(1:3)))
       second = 0
       if (day .eq. 1) second = 3600 * before(5) + 60 * before(6) + before(7)
       last = 86399
       if (day .eq. 2 .or. all(before(1:3) .eq. after(1:3))) last = 3600 * after(5) + 60 * after(6) + after(7)
       do while (second .le. last)
          if (day .eq. 1) then
             write(stamp, '(3i2.2,"_",3i2.2)') before(1) - 2000, before(2), before(3),   &
                  second / 3600, mod(second / 60, 60), mod(second, 60)
          else
             write(stamp, '(3i2.2,"_",3i2.2)') after(1) - 2000, after(2), after(3),      &
                  second / 3600, mod(second / 60, 60), mod(second, 60)
          end if
          name = trim(datadir) // '/' // trim(hiscall) // '_' // stamp // '.pcoeff'
          inquire(file=trim(name), exist=exists)
          if (exists .and. .not. any(written(1:nwritten) .eq. name)) then
             if (nwritten .eq. size(written)) stop 8
             nwritten = nwritten + 1
             written(nwritten) = name
             new_coefficient_file = .true.
             return
          end if
          second = second + 1
       end do
    end do
  end function new_coefficient_file

end program msk144_gui_model
