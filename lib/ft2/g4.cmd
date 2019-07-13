gcc -c ft2audio.c
gcc -c ptt.c
gfortran -c ../77bit/packjt77.f90
gfortran -c ../wavhdr.f90
gfortran -c ../crc.f90
gfortran -o ft2 -fbounds-check -fno-second-underscore -ffpe-trap=invalid,zero -Wall -Wno-conversion -Wno-character-truncation ft2.f90 ft2_iwave.f90 ft2_decode.f90 getcandidates2.f90 ft2audio.o ptt.o /JTSDK/wsjtx-output/qt55/2.1.0/Release/build/libwsjt_fort.a /JTSDK/wsjtx-output/qt55/2.1.0/Release/build/libwsjt_cxx.a libportaudio.a ../libfftw3f_win.a -lwinmm
rm *.o *.mod
