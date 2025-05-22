subroutine save_echo_params(nDopTotal,nDopAudio,nfrit,f1,fspread,ntonespacing, &
     itone,id2,idir)

  integer*2 id2(15),id2a(15)
  integer*4 itone(6)
  integer*1 itone0(6)
  equivalence (nDopTotal0,id2a(1))
  equivalence (nDopAudio0,id2a(3))
  equivalence (nfrit0,id2a(5))
  equivalence (f10,id2a(7))
  equivalence (fspread0,id2a(9))
  equivalence (ntonespacing0,id2a(11))
  equivalence (itone0,id2a(13))
  
  if(idir.gt.0) then
     nDopTotal0=nDopTotal
     nDopAudio0=nDopAudio
     nfrit0=nfrit
     f10=f1
     fspread0=fspread
     ntonespacing0=ntonespacing
     itone0=itone
     id2=id2a
  else
     id2a=id2
     nDopTotal=nDopTotal0
     nDopAudio=nDopAudio0
     nfrit=nfrit0
     f1=f10
     fspread=fspread0
     ntonespacing=ntonespacing0
     if(any(itone0.lt.0) .or. any(itone0.gt.36)) itone0=0
     itone=itone0
  endif

  return
end subroutine save_echo_params
