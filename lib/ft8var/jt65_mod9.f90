module jt65_mod9 
! stores callsign DB in memory for false decodes checking

  save
  
  parameter (MAXC=200000)
! supporting 7-char Australian callsigns
  character*7 call0c(11500),calld(11100),callef(9000),callgh(7500),calli(7600),callj(10000),callk(24000),calllm(6500), &
              calln(9400),callo(6700),callpq(5800),callr(6500),callst(7500),calluv(10400),callw(13700),callxz(5800)
  character callsign*12
  character*180 line
  integer ncall0c,ncalld,ncallef,ncallgh,ncalli,ncallj,ncallk,ncalllm,ncalln,ncallo,ncallpq,ncallr,ncallst,ncalluv, &
          ncallw,ncallxz
  logical ldbvalid
  data ldbvalid/.true./
  data ncall0c,ncalld,ncallef,ncallgh,ncalli,ncallj,ncallk,ncalllm,ncalln,ncallo,ncallpq,ncallr,ncallst,ncalluv,ncallw, &
       ncallxz/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0/

end module jt65_mod9

!callj dim was 9000; increased 20250709 to accommodate size of new ALLCALL7.TXT file dated 20250619
