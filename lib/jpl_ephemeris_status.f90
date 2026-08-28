module jpl_ephemeris_status
  implicit none

  integer, parameter :: JPL_STATUS_OK = 0
  integer, parameter :: JPL_STATUS_IO_ERROR = 1
  integer, parameter :: JPL_STATUS_INVALID_HEADER = 2
  integer, parameter :: JPL_STATUS_OUT_OF_RANGE = 3
  integer, parameter :: JPL_STATUS_NOT_AVAILABLE = 4
  integer, parameter :: JPL_STATUS_INVALID_REQUEST = 5

  integer, parameter :: EPHEMERIS_JPL = 0
  integer, parameter :: EPHEMERIS_ANALYTIC_FALLBACK = 1
  integer, parameter :: EPHEMERIS_INVALID_INPUT = 2
  integer, parameter :: EPHEMERIS_UNAVAILABLE = 3

  integer, save :: configuration_generation = 0
  integer, save :: jpl_reader_generation = 0

contains

  subroutine reset_jpl_reader()
    configuration_generation = configuration_generation + 1
  end subroutine reset_jpl_reader

  subroutine note_jpl_reader_change()
    jpl_reader_generation = jpl_reader_generation + 1
  end subroutine note_jpl_reader_change

end module jpl_ephemeris_status
