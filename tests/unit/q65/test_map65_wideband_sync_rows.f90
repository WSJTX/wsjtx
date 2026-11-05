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

  call test_candidate_search()
  call test_narrow_window_score_scale()

contains

  subroutine reset_search_floor()
    integer :: bin,row
    ss(:,1:jz,ia:ib)=1.0
    savg(:,ia:ib)=real(jz)
    do bin=ia,ib
       do row=1,jz
          ss(1,row,bin)=1.0+0.002*real(mod(17*row+13*bin,23))
       enddo
       savg(1,bin)=sum(ss(1,1:jz,bin))
    enddo
  end subroutine reset_search_floor

  subroutine add_search_sync(bin,level)
    integer, intent(in) :: bin
    real, intent(in) :: level
    integer :: symbol,row,offset
    do symbol=1,size(expanded_sync)
       row=expanded_sync(symbol)+lag
       do offset=0,2
          if(row+offset<=jz) ss(1,row+offset,bin)=ss(1,row+offset,bin)+level
       enddo
    enddo
    savg(1,bin)=sum(ss(1,1:jz,bin))
  end subroutine add_search_sync

  subroutine add_jt65_sync(bin,ooo)
    integer, intent(in) :: bin
    logical, intent(in) :: ooo
    integer, parameter :: standard_symbols(63) = [ &
         1,4,5,9,10,11,12,13,14,16,18,22,24,25,28,32, &
         33,34,37,38,39,40,42,43,45,46,47,48,52,53,55,57, &
         59,60,63,64,66,68,70,73,80,81,89,90,92,95,97,98, &
         100,102,104,107,108,111,114,119,120,121,122,123,124,125,126]
    integer :: symbol,row
    do symbol=1,126
       if(ooo .eqv. any(standard_symbols==symbol)) cycle
       row=2*(symbol-1)+1+lag
       ss(1,row:row+1,bin)=ss(1,row:row+1,bin)+50.0
    enddo
    savg(1,bin)=sum(ss(1,1:jz,bin))
  end subroutine add_jt65_sync

  subroutine assert_one_at(bin,description)
    integer, intent(in) :: bin
    character(len=*), intent(in) :: description
    integer :: index
    logical :: matches
    matches=.false.
    do index=1,ncand
       if(candidates(index)%iflip==0 .and. &
            abs(candidates(index)%f-0.001*(bin-1)*df)<0.0005*df .and. &
            abs(candidates(index)%xdt-(lag*2048.0/11025.0-1.0))<0.001) matches=.true.
    enddo
    if(.not.matches) then
       print '(a)', 'FAIL: '//description
       error stop 1
    endif
  end subroutine assert_one_at

  subroutine test_candidate_search()
    type(candidate) :: first
    integer :: first_count, second_bin

    second_bin=target_bin+120
    call reset_search_floor()
    call add_search_sync(target_bin,50.0)
    call get_candidates(ss,savg,.false.,jz,1,2,0,1,candidates,ncand)
    if(ncand/=1) error stop 'one MAP65 Q65 sync candidate was not isolated'
    call assert_one_at(target_bin,'one MAP65 Q65 candidate frequency and time')
    first_count=ncand
    first=candidates(1)

    call reset_search_floor()
    call add_search_sync(second_bin,50.0)
    call get_candidates(ss,savg,.false.,jz,1,2,0,1,candidates,ncand)
    if(ncand/=1) error stop 'alternate MAP65 candidate was not isolated'
    call assert_one_at(second_bin,'alternate MAP65 candidate frequency and time')

    call reset_search_floor()
    call add_search_sync(target_bin,50.0)
    call get_candidates(ss,savg,.false.,jz,1,2,0,1,candidates,ncand)
    if(ncand/=first_count .or. candidates(1)%f/=first%f .or. &
         candidates(1)%xdt/=first%xdt .or. candidates(1)%snr/=first%snr .or. &
         candidates(1)%iflip/=first%iflip) error stop 'MAP65 A-B-A candidate changed'

    ss(2:4,1:jz,ia:ib)=1.0e20
    savg(2:4,ia:ib)=1.0e20
    call get_candidates(ss,savg,.false.,jz,1,2,0,1,candidates,ncand)
    if(ncand/=first_count .or. candidates(1)%f/=first%f .or. &
         candidates(1)%snr/=first%snr) error stop 'inactive MAP65 polarization affected search'

    call reset_search_floor()
    call add_search_sync(target_bin,50.0)
    call add_search_sync(second_bin,45.0)
    call get_candidates(ss,savg,.false.,jz,1,2,0,1,candidates,ncand)
    if(ncand/=2) error stop 'separated MAP65 Q65 signals did not survive'

    call reset_search_floor()
    call add_search_sync(target_bin,50.0)
    call add_search_sync(target_bin+2,20.0)
    call get_candidates(ss,savg,.false.,jz,1,2,0,1,candidates,ncand)
    if(ncand/=1) error stop 'nearby MAP65 peaks did not collapse to one candidate'
    call assert_one_at(target_bin,'stronger MAP65 nearby peak survived')

    call reset_search_floor()
    ss(1,1:jz,target_bin)=ss(1,1:jz,target_bin)+80.0
    savg(1,target_bin)=sum(ss(1,1:jz,target_bin))
    call get_candidates(ss,savg,.false.,jz,1,2,0,1,candidates,ncand)
    if(ncand/=0) error stop 'continuous MAP65 carrier entered the Q65 candidate list'

    call reset_search_floor()
    call add_jt65_sync(second_bin,.false.)
    call get_candidates(ss,savg,.false.,jz,1,2,0,1,candidates,ncand)
    if(sync(second_bin)%iflip/=1) error stop 'standard JT65 sync fixture was not recognized'
    if(ncand/=0) error stop 'standard JT65 sync entered Q65-only search'

    call reset_search_floor()
    call add_jt65_sync(second_bin,.true.)
    call get_candidates(ss,savg,.false.,jz,1,2,0,1,candidates,ncand)
    if(sync(second_bin)%iflip/=-1) error stop 'OOO JT65 sync fixture was not recognized'
    if(ncand/=0) error stop 'OOO JT65 sync entered Q65-only search'

    call reset_search_floor()
    call add_search_sync(target_bin,50.0)
    call get_candidates(ss,savg,.false.,jz,2,3,0,1,candidates,ncand)
    if(ncand/=0) error stop 'MAP65 candidate outside search band was admitted'
  end subroutine test_candidate_search

  ! A search of only part of the span must score a signal as the whole-span
  ! search does, even when interference fills that part.
  subroutine test_narrow_window_score_scale()
    integer :: lo, hi, wlo, whi, tbin, bin, row
    real :: full_score, bare_score, ref_score

    lo=nint(1000.0/df)+1
    hi=nint(90000.0/df)+1
    wlo=nint(50000.0/df)+1
    whi=nint(51000.0/df)+1
    tbin=(wlo+whi)/2
    ss(:,1:jz,lo:hi)=1.0
    savg(:,lo:hi)=real(jz)
    do bin=lo,hi
       do row=1,jz
          ss(1,row,bin)=1.0+0.002*real(mod(17*row+13*bin,23))
       enddo
       savg(1,bin)=sum(ss(1,1:jz,bin))
    enddo
    do bin=wlo,whi
       if(abs(bin-tbin)>30) call add_search_sync(bin,30.0)
    enddo
    call add_search_sync(tbin,50.0)

    call wb_sync(ss,savg,.false.,.true.,jz,1,90)
    full_score=sync(tbin)%ccfmax
    call wb_sync(ss,savg,.false.,.true.,jz,50,51)
    bare_score=sync(tbin)%ccfmax
    call wb_sync(ss,savg,.false.,.true.,jz,50,51,1,90)
    ref_score=sync(tbin)%ccfmax

    if(bare_score>0.5*full_score) error stop 'fixture: interference did not lower the narrow-window score'
    if(abs(ref_score-full_score)>0.02*full_score) &
         error stop 'narrow search with a reference span changed the signal score'
  end subroutine test_narrow_window_score_scale
end program test_map65_wideband_sync_rows
