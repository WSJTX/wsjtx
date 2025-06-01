subroutine save_echo_params(nDopTotal,nDopAudio,nfrit,f1,fspread,id2,idir)

  integer*2 id2(10)
  integer*2 id2a(10)
  equivalence (nDopTotal0,id2a(1))
  equivalence (nDopAudio0,id2a(3))
  equivalence (nfrit0,id2a(5))
  equivalence (f10,id2a(7))
  equivalence (fspread0,id2a(9))

  if(idir.gt.0) then
     nDopTotal0=nDopTotal
     nDopAudio0=nDopAudio
     nfrit0=nfrit
     f10=f1
     fspread0=fspread
     id2=id2a
  else
     id2a=id2
     id2(1:10)=0
     nDopTotal=nDopTotal0
     nDopAudio=nDopAudio0
     nfrit=nfrit0
     f1=f10
     fspread=fspread0
  endif

  return
end subroutine save_echo_params
