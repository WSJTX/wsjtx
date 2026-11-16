! SPDX-License-Identifier: GPL-3.0-or-later
! The stream's receive chain, on the thread that reads its input, ahead of
! every decoder and every row: configure's receive keys (lib/streaming_emit.f90
! states them) and the GUI's reference-spectrum equalization
! (lib/refspectrum.f90). lib/streaming_io.f90 hands it the audio a GUI block
! at a time: a period's 3456-sample blocks (MSK144's 3584) from the period's
! start, as dataSink receives them, and a JTTY session's 3456-sample blocks
! from its first sample, as the GUI's JTTY receive path does; a block cut
! short by a decode trigger goes in two parts. As ReferenceSpectrumInput
! (ReferenceSpectrum.hpp) does, it resets the filter's history at the first
! block of a period or session and whenever the mode in force changes, and
! filters each block in chunks of at most 3456 samples. Only live audio is
! equalized, as the GUI equalizes only its live audio. With prepared_audio
! the samples it hands on go back to the client (lib/streaming_io.f90 and
! lib/streaming_jtty.f90 send them).
module streaming_receive
  use, intrinsic :: iso_fortran_env, only: int16
  use, intrinsic :: iso_c_binding, only: c_char, c_int, c_null_char
  use prog_args, only: data_dir
  use four2a_patience, only: patience_override
  use refspectrum_file, only: refspec_status, REFSPEC_WRITTEN, REFSPEC_LOADED, &
       REFSPEC_WRITE_FAILED, REFSPEC_READ_FAILED
  use streaming_control, only: configure_fields, REF_SPECTRUM_OFF,          &
       REF_SPECTRUM_MEASURE, REF_SPECTRUM_APPLY
  use streaming_emit, only: streaming_emit_ref_spectrum,                     &
       streaming_emit_ref_spectrum_error
  implicit none
  private

  public :: receive_configure, receive_blocks, receive_equalizes, receive_equalize

  integer, parameter :: NFFT = 6912, CHUNK = NFFT / 2

  ! The keys as accepted configures set them.
  type, public :: receive_settings
     logical :: spectrum = .false.
     logical :: low_sidelobes = .true.
     integer :: ref_spectrum = REF_SPECTRUM_OFF
     logical :: prepared_audio = .false.
  end type receive_settings
  type(receive_settings), public, protected, save :: receive

  ! The mode the last block was equalized in (-1: none yet).
  integer, save :: mode_ = -1
  ! A ref_spectrum_file error went out since ref_spectrum last changed.
  logical, save :: error_sent_ = .false.
  integer(int16), save :: work_(NFFT)

  interface
     subroutine refspectrum(id2, ninput, bclear, brefspec, buseref, fname)
       import :: int16
       integer(int16),   intent(inout) :: id2(*)
       integer,          intent(in)    :: ninput
       logical(1),       intent(in)    :: bclear, brefspec, buseref
       character(len=*), intent(in)    :: fname
     end subroutine refspectrum
     function c_remove(path) result(status) bind(C, name='stream_remove_file')
       import :: c_char, c_int
       character(kind=c_char), intent(in) :: path(*)
       integer(c_int) :: status
     end function c_remove
  end interface

contains

  ! An accepted configure's receive keys; a clear before the frame's
  ! ref_spectrum.
  subroutine receive_configure(cfg)
    type(configure_fields), intent(in) :: cfg
    if (cfg%ref_spectrum_clear_set) then
       if (cfg%ref_spectrum_clear) call clear_()
    end if
    if (cfg%ref_spectrum_set) call set_ref_(cfg%ref_spectrum)
    if (cfg%spectrum_set) receive%spectrum = cfg%spectrum
    if (cfg%low_sidelobes_set) receive%low_sidelobes = cfg%low_sidelobes
    if (cfg%prepared_audio_set) receive%prepared_audio = cfg%prepared_audio
  end subroutine receive_configure

  ! Whether rows, equalization or the prepared audio are asked for, so that
  ! period modes and MSK144 hand their audio over in GUI blocks (JTTY only
  ! while equalizing).
  logical function receive_blocks()
    receive_blocks = receive%spectrum .or. receive%ref_spectrum .ne. REF_SPECTRUM_OFF &
         .or. receive%prepared_audio
  end function receive_blocks

  ! Whether the chain equalizes or measures the audio: live audio, while
  ! ref_spectrum is not off.
  logical function receive_equalizes(live)
    logical, intent(in) :: live
    receive_equalizes = live .and. receive%ref_spectrum .ne. REF_SPECTRUM_OFF
  end function receive_equalizes

  ! A block's samples, or the next part of one, in place; first: they start
  ! a period or a JTTY session.
  subroutine receive_equalize(samples, live, first)
    integer(int16), intent(inout) :: samples(:)
    logical,        intent(in)    :: live, first
    integer :: mode, offset, count
    mode = REF_SPECTRUM_OFF
    if (receive_equalizes(live)) mode = receive%ref_spectrum
    if (first .or. mode .ne. mode_) call call_(0, mode)
    mode_ = mode
    if (mode .eq. REF_SPECTRUM_OFF) return
    do offset = 0, size(samples) - 1, CHUNK
       count = min(CHUNK, size(samples) - offset)
       work_(1:count) = samples(offset + 1:offset + count)
       call call_(count, mode)
       samples(offset + 1:offset + count) = work_(1:count)
    end do
  end subroutine receive_equalize

  ! ref_spectrum set; leaving equalization resets the filter's history now,
  ! as the GUI's next block would.
  subroutine set_ref_(mode)
    integer, intent(in) :: mode
    if (mode .ne. receive%ref_spectrum) error_sent_ = .false.
    receive%ref_spectrum = mode
    if (mode .eq. REF_SPECTRUM_OFF .and. mode_ .gt. REF_SPECTRUM_OFF) then
       call call_(0, REF_SPECTRUM_OFF)
       mode_ = REF_SPECTRUM_OFF
    end if
  end subroutine set_ref_

  ! The measurement reset and refspec.dat deleted, as the GUI's Erase does,
  ! then off, which the GUI's Erase is not while it measures.
  subroutine clear_()
    logical(1) :: clear, off
    clear = .true.
    off = .false.
    call refspectrum(work_, 0, clear, off, off, path_())
    call set_ref_(REF_SPECTRUM_OFF)
    error_sent_ = .false.
    if (c_remove(path_() // c_null_char) .ne. 0) call file_error_('delete', .false.)
  end subroutine clear_

  ! refspectrum on work_(1:count) in mode, planning its FFTs as the GUI's
  ! process does (patience 0); then what it did with refspec.dat.
  subroutine call_(count, mode)
    integer, intent(in) :: count, mode
    logical(1) :: clear, measure, apply
    real :: syellow(6827), ref(0:CHUNK), filter(0:CHUNK)
    common/spectra/syellow, ref, filter
    integer :: patience
    clear = .false.
    measure = mode .eq. REF_SPECTRUM_MEASURE
    apply = mode .eq. REF_SPECTRUM_APPLY
    patience = patience_override
    patience_override = 0
    call refspectrum(work_, count, clear, measure, apply, path_())
    patience_override = patience
    select case (refspec_status)
    case (REFSPEC_WRITTEN, REFSPEC_LOADED)
       call streaming_emit_ref_spectrum(12000.0 / NFFT, ref(1:CHUNK))
    case (REFSPEC_WRITE_FAILED)
       ! The curve is measured whether or not the file was written, and the
       ! GUI draws it either way.
       call streaming_emit_ref_spectrum(12000.0 / NFFT, ref(1:CHUNK))
       call file_error_('write', .true.)
    case (REFSPEC_READ_FAILED)
       call file_error_('read', .true.)
    end select
  end subroutine call_

  ! row: at a block, out as the rows go; otherwise in the configure's place.
  subroutine file_error_(operation, row)
    character(len=*), intent(in) :: operation
    logical,          intent(in) :: row
    if (error_sent_) return
    error_sent_ = .true.
    call streaming_emit_ref_spectrum_error(operation, path_(), row)
  end subroutine file_error_

  function path_() result(path)
    character(len=:), allocatable :: path
    path = trim(data_dir) // '/refspec.dat'
  end function path_

end module streaming_receive
