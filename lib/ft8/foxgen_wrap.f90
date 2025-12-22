subroutine foxgen_wrap(msg40,msgbits,itone)

  parameter (NN=79,ND=58,KK=77,NSPS=4*1920)
  parameter (NWAVE=(160+2)*134400*4) !the biggest waveform we generate (FST4-1800)

  character*40 msg40,cmsg
  character*12 mycall12
  integer*1 msgbits(KK),msgbits2
  integer itone(NN)
  common/foxcom/wave(NWAVE),nslots,nfreq,i3bit(5),cmsg(5),mycall12
  common/foxcom2/itone2(NN),msgbits2(KK)

  nslots=1
  nfreq=300
  i1=index(msg40,'<')
  i2=index(msg40,'>')
  mycall12=msg40(i1+1:i2-1)//'    '
  cmsg(1)=msg40
  i3bit(1)=1
  call foxgen()
  msgbits=msgbits2
  itone=itone2

  return
end subroutine foxgen_wrap
