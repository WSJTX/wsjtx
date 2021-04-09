      subroutine write_char(c, iunit)
      character c
      integer iunit
      write(iunit,1000) c
 1000 format(a,$)
      end      

      subroutine export_wisdom_to_file(iunit)
      integer iunit
      external write_char
c      call dfftw_export_wisdom(write_char, iunit)
      call sfftw_export_wisdom(write_char, iunit)
      end

      subroutine read_char(ic, iunit)
      integer ic
      integer iunit
      character*256 buf
      save buf
      integer ibuf
      data ibuf/257/
      save ibuf
      if (ibuf .lt. 257) then
         ic = ichar(buf(ibuf:ibuf))
         ibuf = ibuf + 1
         return
      endif
      read(iunit,1000,end=10) buf
 1000 format(a256)
      ic = ichar(buf(1:1))
      ibuf = 2
      return
 10   ic = -1
      ibuf = 257
      rewind iunit
      return
      end
      
      subroutine import_wisdom_from_file(isuccess, iunit)
      integer isuccess
      integer iunit
      external read_char
c      call dfftw_import_wisdom(isuccess, read_char, iunit)
      call sfftw_import_wisdom(isuccess, read_char, iunit)
      end
