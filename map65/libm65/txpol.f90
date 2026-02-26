subroutine txpol(xpol,decoded,mygrid,npol,nxant,ntxpol,cp)

!  If Tx station's grid is in decoded message, compute optimum TxPol
  character*22 decoded
  character*6 mygrid,grid
  character*1 cp
  logical xpol

  ntxpol=0
  i1=index(decoded,' ')
  i2=index(decoded(i1+1:),' ') + i1
  grid='      '
  if(i2.ge.8 .and. i2.le.18) grid=decoded(i2+1:i2+4)//'mm'
  ntxpol=0
  cp=' '
  if(xpol .and.grid(1:4).ne.'RR73') then
     if(grid(1:1).ge.'A' .and. grid(1:1).le.'R' .and.           &
          grid(2:2).ge.'A' .and. grid(2:2).le.'R' .and.         &
          grid(3:3).ge.'0' .and. grid(3:3).le.'9' .and.         &
          grid(4:4).ge.'0' .and. grid(4:4).le.'9') then                 
        ntxpol=mod(npol-nint(2.0*dpol(mygrid,grid))+720,180)
        if(nxant.eq.0) then
           cp='H'
           if(ntxpol.gt.45 .and. ntxpol.le.135) cp='V'
        else
           cp='/'
           if(ntxpol.ge.90 .and. ntxpol.lt.180) cp='\'
        endif
     endif
  endif

  return
end subroutine txpol
