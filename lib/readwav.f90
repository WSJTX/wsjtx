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
   contains
     procedure :: read
  end type wav_header

  private
contains
  subroutine read (this, filename)
    implicit none

    type riff_descriptor
       character(len=4) :: id
       integer :: size
    end type riff_descriptor

    class(wav_header), intent(inout) :: this
    character(len=*), intent(in) :: filename

    integer :: filepos
    integer :: ios
    type(riff_descriptor) :: desc
    character(len=4) :: riff_type

    this%lun=26
    open (unit=this%lun, file=filename, access='stream',status='old')
    read (unit=this%lun, iostat=ios) desc,riff_type
    if (ios .ne. 0) stop 'readwav: error reading RIFF header (truncated or invalid WAV file)'
    inquire (unit=this%lun, pos=filepos)
    do
       read (unit=this%lun, pos=filepos, iostat=ios) desc
       if (ios .ne. 0) stop 'readwav: no data chunk found (truncated or malformed WAV file)'
       inquire (unit=this%lun, pos=filepos)
       if (desc%id .eq. 'fmt ') then
          read (unit=this%lun) this%audio_format
       else if (desc%id .eq. 'data') then
          this%data_size = desc%size
          exit
       end if
       if (desc%size .lt. 0) stop 'readwav: invalid (negative) chunk size in WAV file'
       filepos = filepos + (desc%size + 1) / 2 * 2 ! pad to even alignment
       if (filepos .lt. 1) stop 'readwav: invalid chunk size in WAV file'
    end do
    return
  end subroutine read
end module readwav
