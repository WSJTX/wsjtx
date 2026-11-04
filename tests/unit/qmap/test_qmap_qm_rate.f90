program test_qmap_qm_rate
  ! read_qm must refuse a .qm file recorded at another IQ rate than the one in use, and must
  ! report the rate it found in the header.  QMAP is running at the default 96 kHz here.
  use qmap_params, only: qm_file_rate
  implicit none

  call check('192 kHz, full minute',    192000, 1, 11520000, -3, 192000)
  call check('192 kHz, first half',     192000, 1,  5760000, -3, 192000)   !same samples as 96 kHz, full minute
  call check('192 kHz, second half',    192000, 5760001, 11520000, -3, 192000)
  call check('128 kHz, first half',     128000, 1,  3840000, -3, 128000)
  call check('256 kHz, full minute',    256000, 1, 15360000, -3, 256000)
  ! A header without a rate comes from a 96 kHz file: it is accepted, and the data are read (3 = full minute).
  call check('96 kHz (no rate stored)', 0,      1,  5760000,  3,  96000)
  print '(a)', 'QMAP .qm rate check passed.'

contains

  subroutine check(label, nrate, ia, ib, iret_expected, rate_expected)
    character(len=*), intent(in) :: label
    integer, intent(in) :: nrate, ia, ib, iret_expected, rate_expected
    character(len=24) :: prog_id
    character(len=12) :: mycall
    character(len=6)  :: mygrid
    real(8) :: fcenter
    integer(1), allocatable :: data(:,:)
    integer :: nutc, ntx30a, ntx30b, ndop00, ndop58, nxtra(15), iret, u
    real :: fac0
    character(len=*), parameter :: fname = 'test_qmap_qm_rate.qm'

    prog_id = 'QMAP test'
    mycall = 'W3SZ'
    mygrid = 'FN20'
    fcenter = 1296.1d0
    nutc = 1200
    ntx30a = 0
    ntx30b = 0
    ndop00 = 0
    ndop58 = 0
    fac0 = 1.0
    nxtra = 0
    nxtra(1) = nrate
    open(newunit=u, file=fname, status='replace', access='stream')
    write(u) prog_id, mycall, mygrid, fcenter, nutc, ntx30a, ntx30b, ndop00, ndop58, ia, ib, fac0, nxtra
    if(iret_expected > 0) then          !A file that will be read needs its samples
       allocate(data(2, ib-ia+1))
       data = 0
       write(u) data
       deallocate(data)
    endif
    close(u, status='keep')

    iret = 99
    qm_file_rate = 0
    call read_qm(fname, iret)
    if(iret /= iret_expected .or. qm_file_rate /= rate_expected) then
       print '(a,a,a,i0,a,i0,a,i0,a,i0)', 'FAIL ', label, ': iret=', iret, ' (want ', iret_expected, &
            '), rate=', qm_file_rate, ' (want ', rate_expected
       error stop 1
    endif
    open(newunit=u, file=fname, status='old')
    close(u, status='delete')
  end subroutine check

end program test_qmap_qm_rate
