character*17 function cdatetime()
  character cdate*8,ctime*10
  call date_and_time(cdate,ctime)
  cdatetime=cdate(3:8)//'_'//ctime
  return
end function cdatetime
