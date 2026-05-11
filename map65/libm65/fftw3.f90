module fftw3_constants
  implicit none

  integer, parameter :: FFTW_R2HC              = 0
  integer, parameter :: FFTW_HC2R              = 1
  integer, parameter :: FFTW_DHT               = 2
  integer, parameter :: FFTW_REDFT00           = 3
  integer, parameter :: FFTW_REDFT01           = 4
  integer, parameter :: FFTW_REDFT10           = 5
  integer, parameter :: FFTW_REDFT11           = 6
  integer, parameter :: FFTW_RODFT00           = 7
  integer, parameter :: FFTW_RODFT01           = 8
  integer, parameter :: FFTW_RODFT10           = 9
  integer, parameter :: FFTW_RODFT11           = 10

  integer, parameter :: FFTW_FORWARD           = -1
  integer, parameter :: FFTW_BACKWARD          = +1

  integer, parameter :: FFTW_MEASURE           = 0
  integer, parameter :: FFTW_DESTROY_INPUT     = 1
  integer, parameter :: FFTW_UNALIGNED         = 2
  integer, parameter :: FFTW_CONSERVE_MEMORY   = 4
  integer, parameter :: FFTW_EXHAUSTIVE        = 8
  integer, parameter :: FFTW_PRESERVE_INPUT    = 16
  integer, parameter :: FFTW_PATIENT           = 32
  integer, parameter :: FFTW_ESTIMATE          = 64
  integer, parameter :: FFTW_ESTIMATE_PATIENT  = 128
  integer, parameter :: FFTW_BELIEVE_PCOST     = 256
  integer, parameter :: FFTW_DFT_R2HC_ICKY     = 512
  integer, parameter :: FFTW_NONTHREADED_ICKY  = 1024
  integer, parameter :: FFTW_NO_BUFFERING      = 2048
  integer, parameter :: FFTW_NO_INDIRECT_OP    = 4096
  integer, parameter :: FFTW_ALLOW_LARGE_GENERIC = 8192
  integer, parameter :: FFTW_NO_RANK_SPLITS    = 16384
  integer, parameter :: FFTW_NO_VRANK_SPLITS   = 32768
  integer, parameter :: FFTW_NO_VRECURSE       = 65536
  integer, parameter :: FFTW_NO_SIMD           = 131072

end module fftw3_constants
