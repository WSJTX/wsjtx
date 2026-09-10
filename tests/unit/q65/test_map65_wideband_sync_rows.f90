program test_map65_wideband_sync_rows
  use wideband_sync, only: candidate,get_candidates,init_wideband_sync,outside_row_count,sync,wb_sync
  use npar_ptrs_mod, only: nfft_active, nrate_active
  implicit none

  integer, parameter :: jz=280, lag=11
  integer :: expanded_sync(22)
  integer :: ia, ib, i, j, k, ncand, offset, peak(1), target_bin
  real :: angle_error,ccf_before,df,response(4),xdt_before
  real, allocatable :: savg(:,:), ss(:,:,:)
  type(candidate) :: candidates(50)
  logical :: found

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
  call wb_sync(ss,savg,.false.,.true.,jz,1,2)
  peak=maxloc(sync(ia:ib)%ccfmax)
  if (peak(1)+ia-1 /= target_bin) error stop 'late Q65 partial sync peak was not found'
  if (sync(target_bin)%iflip /= 0) error stop 'late Q65 partial sync was misclassified'
  if (abs(sync(target_bin)%xdt-(lag*2048.0/11025.0-1.0)) > 0.001) &
       error stop 'late Q65 partial sync used the wrong lag'

  ccf_before=sync(target_bin)%ccfmax
  xdt_before=sync(target_bin)%xdt
  ss(1,jz+1:322,ia:ib)=1.0e20
  call wb_sync(ss,savg,.false.,.true.,jz,1,2)

  if (sync(target_bin)%ccfmax /= ccf_before) &
       error stop 'unavailable spectrum rows changed Q65 sync strength'
  if (sync(target_bin)%xdt /= xdt_before) &
       error stop 'unavailable spectrum rows changed Q65 sync lag'

  if (outside_row_count(24,300,280) /= 24) &
       error stop 'early Q65 carrier baseline used the wrong row count'
  if (outside_row_count(-1,276,280) /= 5) &
       error stop 'negative leading rows changed the carrier baseline count'

  do i=ia,ib
     do j=1,jz
        ss(:,j,i)=1.0+0.001*[real(mod(37*j+17*i,23)),real(mod(31*j+13*i,19)), &
             real(mod(29*j+11*i,17)),real(mod(23*j+7*i,13))]
     enddo
     savg(:,i)=[sum(ss(1,1:jz,i)),sum(ss(2,1:jz,i)), &
          sum(ss(3,1:jz,i)),sum(ss(4,1:jz,i))]
  enddo
  response=[cos(22.5*acos(-1.0)/180.0)**2,cos(-22.5*acos(-1.0)/180.0)**2, &
       cos(-67.5*acos(-1.0)/180.0)**2,cos(-112.5*acos(-1.0)/180.0)**2]
  do j=1,size(expanded_sync)
     k=expanded_sync(j)+lag
     do offset=0,2
        if (k+offset <= jz) ss(:,k+offset,target_bin)=ss(:,k+offset,target_bin)+50.0*response
     enddo
  enddo
  do i=1,4
     savg(i,target_bin)=sum(ss(i,1:jz,target_bin))
  enddo

  call wb_sync(ss,savg,.true.,.false.,jz,1,2)
  ccf_before=sync(target_bin)%ccfmax
  call get_candidates(ss,savg,.true.,jz,1,2,0,1,candidates,ncand)
  if (sync(target_bin)%ccfmax <= ccf_before) &
       error stop 'continuous polarization did not improve the production Q65 score'
  angle_error=abs(modulo(sync(target_bin)%pol-22.5+90.0,180.0)-90.0)
  if (angle_error > 0.05) error stop 'continuous polarization recovered the wrong physical angle'
  angle_error=abs(modulo(sync(target_bin)%combine_pol-22.5+90.0,180.0)-90.0)
  if (angle_error > 0.05) error stop 'continuous polarization selected the wrong combining angle'
  found=.false.
  do i=1,ncand
     if (candidates(i)%iflip == 0 .and. &
          abs(candidates(i)%f-0.001*(target_bin-1)*df) < 0.0005*df) found=.true.
  enddo
  if (.not.found) error stop 'continuous polarization score did not reach Q65 candidate admission'

  savg(2,ia:ib)=4.0*real(jz)
  savg(4,ia:ib)=0.0
  call wb_sync(ss,savg,.true.,.false.,jz,1,2)
  ccf_before=sync(target_bin)%ccfmax
  call get_candidates(ss,savg,.true.,jz,1,2,0,1,candidates,ncand)
  if (sync(target_bin)%ccfmax /= ccf_before) &
       error stop 'invalid covariance did not preserve the legacy Q65 score'
end program test_map65_wideband_sync_rows
