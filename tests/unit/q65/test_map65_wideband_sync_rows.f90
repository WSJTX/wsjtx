program test_map65_wideband_sync_rows
  use wideband_sync, only: init_wideband_sync, outside_row_count, sync, wb_sync
  use npar_ptrs_mod, only: nfft_active, nrate_active
  implicit none

  integer, parameter :: jz=280, lag=11
  integer :: expanded_sync(22)
  integer :: ia, ib, i, j, k, offset, peak(1), target_bin
  real :: ccf_before, df, xdt_before
  real, allocatable :: savg(:,:), ss(:,:,:)

  expanded_sync = [1,27,37,40,46,69,72,82,85,104,111,121,146,159,175,192, &
       198,211,221,237,243,272]
  nrate_active=96000
  nfft_active=32768
  df=real(nrate_active)/real(nfft_active)
  ia=nint(1000.0/df)+1
  ib=nint(2000.0/df)+1
  target_bin=(ia+ib)/2

  allocate(ss(4,322,nfft_active),savg(4,nfft_active))
  ss=1.0
  savg=real(jz)
  do i=ia,ib
     do j=1,jz
        ss(1,j,i)=1.0+0.001*real(mod(37*j+17*i,23))
     enddo
     savg(1,i)=sum(ss(1,1:jz,i))
  enddo

  do j=1,size(expanded_sync)
     k=expanded_sync(j)+lag
     do offset=0,2
        if (k+offset <= jz) ss(1,k+offset,target_bin)= &
             ss(1,k+offset,target_bin)+50.0
     enddo
  enddo
  savg(1,target_bin)=sum(ss(1,1:jz,target_bin))

  call init_wideband_sync()
  call wb_sync(ss,savg,.false.,jz,1,2)
  peak=maxloc(sync(ia:ib)%ccfmax)
  if (peak(1)+ia-1 /= target_bin) error stop 'late Q65 partial sync peak was not found'
  if (sync(target_bin)%iflip /= 0) error stop 'late Q65 partial sync was misclassified'
  if (abs(sync(target_bin)%xdt-(lag*2048.0/11025.0-1.0)) > 0.001) &
       error stop 'late Q65 partial sync used the wrong lag'

  ccf_before=sync(target_bin)%ccfmax
  xdt_before=sync(target_bin)%xdt
  ss(1,jz+1:322,ia:ib)=1.0e20
  call wb_sync(ss,savg,.false.,jz,1,2)

  if (sync(target_bin)%ccfmax /= ccf_before) &
       error stop 'unavailable spectrum rows changed Q65 sync strength'
  if (sync(target_bin)%xdt /= xdt_before) &
       error stop 'unavailable spectrum rows changed Q65 sync lag'

  if (outside_row_count(24,300,280) /= 24) &
       error stop 'early Q65 carrier baseline used the wrong row count'
  if (outside_row_count(-1,276,280) /= 5) &
       error stop 'negative leading rows changed the carrier baseline count'
end program test_map65_wideband_sync_rows
