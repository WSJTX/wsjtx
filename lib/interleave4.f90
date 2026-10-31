subroutine interleave4(id,ndir)
  use first_call_tsan, only: wsjt_tsan_acquire_first_call,wsjt_tsan_release_first_call
  integer*1 id(0:205),itmp(0:205)
  integer j0(0:205)
  logical first
  data first/.true./
  save first,j0
  logical building

  !$omp atomic read seq_cst
  building=first
  if(building) then
     !$omp critical(first_call)
     call wsjt_tsan_acquire_first_call()
     if(first) then
        k=-1
        do i=0,255
           m=i
           n=iand(m,1)
           n=2*n + iand(m/2,1)
           n=2*n + iand(m/4,1)
           n=2*n + iand(m/8,1)
           n=2*n + iand(m/16,1)
           n=2*n + iand(m/32,1)
           n=2*n + iand(m/64,1)
           n=2*n + iand(m/128,1)
           if(n.le.205) then
              k=k+1
              j0(k)=n
           endif
        enddo
        !$omp atomic write seq_cst
        first=.false.
     endif
     call wsjt_tsan_release_first_call()
     !$omp end critical(first_call)
  endif

  if(ndir.eq.1) then
     do i=0,205
        itmp(j0(i))=id(i)
     enddo
  else
     do i=0,205
        itmp(i)=id(j0(i))
     enddo
  endif

  do i=0,205
     id(i)=itmp(i)
  enddo

  return
end subroutine interleave4
