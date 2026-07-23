!
! readwav - open and read the header of a WAV format file
!
! On successful exit the file is left positioned at the start of the
! data.
!
! Example of usage:
!
!  use readwav
!  integer*2 sample
!  type(wav_header) wav
!  call wav%read ('file.wav')
!  write (*,*) 'Sample rate is: ', wav%audio_format%sample_rate
!  do i=0,wav%data_size
!    read (unit=wav%lun) sample
!    ! process sample
!  end do
!
module readwav
  use, intrinsic :: iso_fortran_env, only: int64
  implicit none

  type format_chunk
     integer*2 audio_format
     integer*2 num_channels
     integer sample_rate
     integer byte_rate
     integer*2 block_align
     integer*2 bits_per_sample
  end type format_chunk
  
  type, public :: wav_header
     integer :: lun
     type(format_chunk) :: audio_format
     integer :: data_size
     integer :: data_bytes_remaining
   contains
     procedure :: read
     procedure :: read_samples
  end type wav_header

  private
contains
  subroutine read (this, filename, status, message)
    implicit none

    type riff_descriptor
       character(len=4) :: id
       integer :: size
    end type riff_descriptor

    class(wav_header), intent(inout) :: this
    character(len=*), intent(in) :: filename
    integer, intent(out), optional :: status
    character(len=*), intent(out), optional :: message

    integer :: ios
    integer(int64) :: filepos, filesize, data_end, nextpos, padded_size
    type(riff_descriptor) :: desc
    character(len=4) :: riff_type
    logical :: format_found

    this%lun=26
    this%data_size = 0
    this%data_bytes_remaining = 0
    this%audio_format = format_chunk(0, 0, 0, 0, 0, 0)
    format_found = .false.
    call set_result(0, '')

    open (unit=this%lun, file=filename, access='stream', form='unformatted', &
         status='old', action='read', iostat=ios)
    if (ios /= 0) then
       call fail('cannot open file')
       return
    end if
    inquire (unit=this%lun, size=filesize)
    if (filesize < 12) then
       call fail('file is too short for a RIFF/WAVE header')
       return
    end if

    read (unit=this%lun, iostat=ios) desc, riff_type
    if (ios /= 0) then
       call fail('cannot read RIFF/WAVE header')
       return
    end if
    if (desc%id /= 'RIFF' .or. riff_type /= 'WAVE') then
       call fail('not a RIFF/WAVE file')
       return
    end if
    inquire (unit=this%lun, pos=filepos)
    do
       if (filepos + 8_int64 > filesize + 1_int64) then
          call fail('missing data chunk')
          return
       end if
       read (unit=this%lun, pos=filepos, iostat=ios) desc
       if (ios /= 0) then
          call fail('cannot read WAV chunk header')
          return
       end if
       inquire (unit=this%lun, pos=filepos)
       if (desc%size < 0) then
          call fail('WAV chunk has an invalid size')
          return
       end if
       data_end = filepos + int(desc%size, int64)
       if (data_end > filesize + 1_int64) then
          call fail('WAV chunk extends beyond end of file')
          return
       end if
       padded_size = int(desc%size, int64)
       if (mod(padded_size, 2_int64) /= 0) padded_size = padded_size + 1_int64
       nextpos = filepos + padded_size
       if (desc%id .eq. 'fmt ') then
          if (desc%size < 16) then
             call fail('fmt chunk is too short')
             return
          end if
          read (unit=this%lun, iostat=ios) this%audio_format
          if (ios /= 0) then
             call fail('cannot read fmt chunk')
             return
          end if
          if (this%audio_format%audio_format /= 1 .or. &
               this%audio_format%num_channels /= 1 .or. &
               this%audio_format%bits_per_sample /= 16) then
             call fail('unsupported WAV format; expected mono 16-bit PCM')
             return
          end if
          if (this%audio_format%sample_rate /= 11025 .and. &
               this%audio_format%sample_rate /= 12000) then
             call fail('unsupported WAV sample rate')
             return
          end if
          format_found = .true.
       else if (desc%id .eq. 'data') then
          if (.not. format_found) then
             call fail('data chunk appears before fmt chunk')
             return
          end if
          this%data_size = desc%size
          this%data_bytes_remaining = desc%size
          call set_result(0, '')
          return
       end if
       if (nextpos > filesize + 1_int64) then
          call fail('WAV chunk extends beyond end of file')
          return
       end if
       filepos = nextpos
    end do

  contains

    subroutine fail(text)
      character(len=*), intent(in) :: text

      close(unit=this%lun, iostat=ios)
      this%lun = -1
      call set_result(1, text)
    end subroutine fail

    subroutine set_result(code, text)
      integer, intent(in) :: code
      character(len=*), intent(in) :: text

      if (present(status)) status = code
      if (present(message)) message = text
    end subroutine set_result
  end subroutine read

  subroutine read_samples(this, samples, samples_read, status, message)
    class(wav_header), intent(inout) :: this
    integer*2, intent(out) :: samples(:)
    integer, intent(out) :: samples_read
    integer, intent(out), optional :: status
    character(len=*), intent(out), optional :: message

    integer :: ios

    samples = 0
    samples_read = min(size(samples), this%data_bytes_remaining / 2)
    if (present(status)) status = 0
    if (present(message)) message = ''
    if (samples_read == 0) return

    read(unit=this%lun, iostat=ios) samples(:samples_read)
    if (ios /= 0) then
      samples_read = 0
      if (present(status)) status = 1
      if (present(message)) message = 'cannot read WAV sample data'
      return
    end if
    this%data_bytes_remaining = this%data_bytes_remaining - 2 * samples_read
  end subroutine read_samples
end module readwav
