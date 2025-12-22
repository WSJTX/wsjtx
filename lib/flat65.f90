subroutine flat65(ss,nhsym,maxhsym,nsz,ref)

  real stmp(nsz)
  real ss(maxhsym,nsz)
  real ref(nsz)

  npct=28                                       !Somewhat arbitrary
  do i=1,nsz
     call pctile(ss(1,i),nhsym,npct,stmp(i))
  enddo

  nsmo=33
  ia=nsmo/2 + 1
  ib=nsz - nsmo/2 - 1
  do i=ia,ib
     call pctile(stmp(i-nsmo/2),nsmo,npct,ref(i))
  enddo
  ref(:ia-1)=ref(ia)
  ref(ib+1:)=ref(ib)
  ref=4.0*ref

  return
end subroutine flat65

      
