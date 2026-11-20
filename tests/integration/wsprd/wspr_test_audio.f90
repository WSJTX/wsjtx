! SPDX-License-Identifier: GPL-3.0-or-later
! Test audio for WSPR decodes.
!
!   wspr_test_audio wav <out.wav> <seed> <message> <Hz> <snr> <start> [<message> <Hz> <snr> <start> ...]
!     120 s of 12 kHz mono int16: each message encoded by genwspr (as wsjt-codec's render
!     does) and rendered by render_fsk, its lowest tone at <Hz>, starting <start> s in, at
!     <snr> dB in 2500 Hz, over noise of standard deviation 1000 from a seeded generator.
!   wspr_test_audio c2 <in.wav> <dir>/<YYMMDD_HHMM>.c2 <dial MHz>
!     The .c2 file WSJT-X writes for a WSPR period (widgets/mainwindow.cpp): wspr_downsample
!     on each 3456-sample block up to the stop, 396*3456 samples, then savec2.
program wspr_test_audio
  use, intrinsic :: iso_fortran_env, only: int16, int32, int64, real32, real64, error_unit
  use waveform_kernels, only: render_fsk
  implicit none
  integer, parameter :: NMAX = 120*12000, NSPS = 8192, NSYM = 162, KSTOP = 396*3456
  real(real64), parameter :: SIGMA = 1000.d0
  integer(int16), allocatable :: id2(:)
  real(real64), allocatable :: x(:)
  real(real32), allocatable :: wave(:)
  character(len=512) :: command, outname, arg
  character(len=22) :: message, msgsent
  integer :: itone(NSYM), nargs, i, i0, n, seed
  real(real64) :: freq, snr, start, amp, dial
  integer :: npatience, nthreads, savec2
  common/patience/npatience,nthreads

  nargs = command_argument_count()
  if (nargs < 1) call usage()
  call get_command_argument(1, command)
  select case (trim(command))
  case ('wav')
     if (nargs < 7 .or. mod(nargs - 3, 4) /= 0) call usage()
     call get_command_argument(2, outname)
     call get_command_argument(3, arg)
     read(arg, *) seed
     allocate(x(NMAX), wave(NSYM*NSPS), id2(NMAX))
     call noise(seed, x)
     do i = 4, nargs, 4
        call get_command_argument(i, message)
        call get_command_argument(i + 1, arg)
        read(arg, *) freq
        call get_command_argument(i + 2, arg)
        read(arg, *) snr
        call get_command_argument(i + 3, arg)
        read(arg, *) start
        call genwspr(message, msgsent, itone)
        call render_fsk(itone, NSPS, 12000.d0, 12000.d0/NSPS, freq, wave=wave)
        ! A tone of amplitude amp has power amp**2/2; the noise puts SIGMA**2*2500/6000 in 2500 Hz.
        amp = sqrt(2.d0*SIGMA**2*2500.d0/6000.d0*10.d0**(snr/10.d0))
        i0 = nint(start*12000.d0)
        n = min(size(wave), NMAX - i0)
        x(i0 + 1:i0 + n) = x(i0 + 1:i0 + n) + amp*wave(1:n)
     end do
     id2 = int(max(-32768.d0, min(32767.d0, anint(x))), int16)
     call write_wav(outname, id2)
  case ('c2')
     if (nargs /= 4) call usage()
     call get_command_argument(2, arg)
     call get_command_argument(3, outname)
     call get_command_argument(4, command)
     read(command, *) dial
     allocate(id2(NMAX))
     call read_wav(arg, id2)
     npatience = 0                       ! FFTW_ESTIMATE: WSJT-X never sets /patience/
     nthreads = 1
     n = 0
     do while (n < KSTOP)
        n = min(n + 3456, KSTOP)
        call wspr_downsample(id2, n)
     end do
     if (savec2(trim(outname), 120, dial) /= 0) then
        write(error_unit, '(a)') 'wspr_test_audio: savec2 failed for ' // trim(outname)
        stop 1
     end if
  case default
     call usage()
  end select

contains

  subroutine usage()
    write(error_unit, '(a)') 'Usage: wspr_test_audio wav <out.wav> <seed> <message> <Hz> <snr> <start> ...'
    write(error_unit, '(a)') '       wspr_test_audio c2 <in.wav> <YYMMDD_HHMM>.c2 <dial MHz>'
    stop 2
  end subroutine usage

  ! Park and Miller's minimal standard generator, twelve uniforms summed for each
  ! sample: integer arithmetic and sums only, so a seed gives the same noise anywhere.
  subroutine noise(seed, x)
    integer, intent(in) :: seed
    real(real64), intent(out) :: x(:)
    integer(int64), parameter :: A = 16807, M = 2147483647
    integer(int64) :: state
    integer :: i, j
    real(real64) :: total
    state = modulo(int(seed, int64), M - 1) + 1
    do i = 1, size(x)
       total = 0
       do j = 1, 12
          state = modulo(A*state, M)
          total = total + real(state, real64)/real(M, real64)
       end do
       x(i) = SIGMA*(total - 6.d0)
    end do
  end subroutine noise

  ! A 44-byte header, as WSJT-X and wsprd read it; hosts are little-endian.
  subroutine write_wav(name, samples)
    character(len=*), intent(in) :: name
    integer(int16), intent(in) :: samples(:)
    integer :: lu
    integer(int32) :: nbytes
    nbytes = 2*size(samples)
    open(newunit=lu, file=trim(name), access='stream', form='unformatted', status='replace')
    write(lu) 'RIFF', 36_int32 + nbytes, 'WAVE', 'fmt ', 16_int32, 1_int16, 1_int16, &
         12000_int32, 24000_int32, 2_int16, 16_int16, 'data', nbytes, samples
    close(lu)
  end subroutine write_wav

  subroutine read_wav(name, samples)
    character(len=*), intent(in) :: name
    integer(int16), intent(out) :: samples(:)
    character(len=44) :: header
    integer :: lu, ios
    samples = 0
    open(newunit=lu, file=trim(name), access='stream', form='unformatted', status='old', &
         iostat=ios)
    if (ios /= 0) then
       write(error_unit, '(a)') 'wspr_test_audio: cannot open ' // trim(name)
       stop 1
    end if
    read(lu) header
    read(lu, iostat=ios) samples         ! a short file leaves the rest zero
    close(lu)
  end subroutine read_wav

end program wspr_test_audio
