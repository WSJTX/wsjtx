MODULE prog_args
  CHARACTER(len=80) :: shm_key
  CHARACTER(len=500) :: ipc_lock_path = ''
  CHARACTER(len=500) :: exe_dir = '.', data_dir = '.', temp_dir = '.',    &
       share_dir = '.' !ft8md added share_dir
  LOGICAL :: lquiet = .false.
  ! Whether the decoders write the result files WSJT-X's worker writes beside
  ! its lines (decoded.txt, avemsg.txt, houndcallers.txt, red.dat,
  ! q65_decodes.txt, fort.77); the codec's stream turns it off, as its lines
  ! carry them (lib/streaming_io.f90). State files are not result files.
  LOGICAL :: result_files = .true.
END MODULE prog_args
