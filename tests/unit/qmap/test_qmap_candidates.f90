program test_qmap_candidates
  use qmap_candidates_mod, only: candidate, getcand2
  implicit none

  integer, parameter :: nfft=32768, first_bin=10000, second_bin=10200
  integer, parameter :: sync_symbols(22) = [1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85]
  real, parameter :: df=96000.0/nfft
  real, allocatable :: ss(:,:), savg(:)
  type(candidate) :: found(50), original(50)
  integer :: ncand, original_count

  allocate(ss(400,nfft),savg(nfft))

  call clear_spectra()
  call add_sync(first_bin,60,0,11,35.0)
  call search(390,10,10,.false.,found,ncand)
  call require(ncand==1,'one Q65-60 candidate')
  call expect_candidate(found(1),first_bin,60,0,11)
  original_count=ncand
  original(1:ncand)=found(1:ncand)

  call clear_spectra()
  call add_sync(second_bin,60,0,7,35.0)
  call search(390,10,10,.false.,found,ncand)
  call require(ncand==1,'alternate Q65-60 candidate')
  call expect_candidate(found(1),second_bin,60,0,7)

  call clear_spectra()
  call add_sync(first_bin,60,0,11,35.0)
  call search(390,10,10,.false.,found,ncand)
  call require(ncand==original_count,'A-B-A count is stable')
  call same_candidate(found(1),original(1))

  ! A missing final average-spectrum bin is the searcher's empty-input sentinel.
  ss=0.0
  savg=0.0
  call search(390,10,10,.false.,found,ncand)
  call require(ncand==0,'empty spectrum clears the previous candidate count')

  call clear_spectra()
  call add_sync(first_bin,60,0,11,35.0)
  call add_sync(second_bin,60,0,7,30.0)
  call search(390,10,10,.false.,found,ncand)
  call require(ncand==2,'separated Q65 signals survive')
  call require(has_candidate(found,ncand,first_bin,60,0) .and. &
       has_candidate(found,ncand,second_bin,60,0),'both separated frequencies survive')

  call clear_spectra()
  call add_sync(first_bin,60,0,11,35.0)
  call add_sync(first_bin+2,60,0,11,12.0)
  call search(390,10,10,.false.,found,ncand)
  call require(ncand==1,'nearby bins form one candidate')
  call expect_candidate(found(1),first_bin,60,0,11)

  call clear_spectra()
  ss(:,first_bin)=ss(:,first_bin)+40.0
  savg(first_bin)=sum(ss(:,first_bin))
  call search(390,10,10,.false.,found,ncand)
  call require(ncand==0,'continuous carrier is not a Q65 candidate')

  call clear_spectra()
  call add_sync(first_bin,30,0,9,35.0)
  call search(130,0,10,.true.,found,ncand)
  call require(ncand==1,'first Q65-30 half is admitted')
  call expect_candidate(found(1),first_bin,30,0,9)
  call search(130,6,10,.true.,found,ncand)
  call require(ncand==0,'first transmit-half gate suppresses reception')
  call search(201,0,10,.true.,found,ncand)
  call require(ncand==0,'first half is unavailable after its row boundary')

  call clear_spectra()
  call add_sync(first_bin,30,1,8,35.0)
  call search(390,10,0,.true.,found,ncand)
  call require(ncand==1,'second Q65-30 half is admitted')
  call expect_candidate(found(1),first_bin,30,1,8)
  call search(390,10,6,.true.,found,ncand)
  call require(ncand==0,'second transmit-half gate suppresses reception')
  call search(329,10,0,.true.,found,ncand)
  call require(ncand==0,'second half is unavailable before its row boundary')

  call clear_spectra()
  call add_sync(first_bin,60,0,11,35.0)
  call search(199,10,10,.false.,found,ncand)
  call require(ncand==0,'Q65-60 requires its available-row gate')
  call search(200,10,10,.false.,found,ncand)
  call require(ncand==1,'Q65-60 opens at its available-row boundary')
  call search_selected(first_bin,found,ncand)
  call require(ncand==1,'selected search includes its frequency bin')
  call search_selected(second_bin,found,ncand)
  call require(ncand==0,'selected search excludes a distant frequency bin')

  print '(a)', 'QMAP candidate contracts passed'

contains

  subroutine clear_spectra()
    integer :: bin,row
    real :: floor(400)
    do row=1,400
       floor(row)=1.0+0.002*real(mod(17*row,23))
    enddo
    do bin=1,nfft
       ss(:,bin)=floor+0.0002*real(mod(bin,17))
       savg(bin)=sum(ss(:,bin))
    enddo
  end subroutine clear_spectra

  subroutine add_sync(bin,period,sequence,lag,power)
    integer, intent(in) :: bin,period,sequence,lag
    real, intent(in) :: power
    integer :: i,row,rows,stride
    rows=4
    stride=4
    if(period==30) then
       rows=2
       stride=2
    endif
    do i=1,size(sync_symbols)
       row=stride*(sync_symbols(i)-1)+1+lag+sequence*200
       ss(row:row+rows-1,bin)=ss(row:row+rows-1,bin)+power
    enddo
    savg(bin)=sum(ss(:,bin))
  end subroutine add_sync

  subroutine search(nhsym,ntx30a,ntx30b,also30,candidates,count)
    integer, intent(in) :: nhsym,ntx30a,ntx30b
    logical, intent(in) :: also30
    type(candidate), intent(out) :: candidates(50)
    integer, intent(out) :: count
    real, allocatable :: fresh_savg(:)
    logical(kind=1) :: search30
    allocate(fresh_savg(nfft))
    fresh_savg=savg
    search30=also30
    count=-1
    call getcand2(ss,fresh_savg,2,0,nhsym,ntx30a,ntx30b,100,0.0,search30,candidates,count)
  end subroutine search

  subroutine search_selected(selected_bin,candidates,count)
    integer, intent(in) :: selected_bin
    type(candidate), intent(out) :: candidates(50)
    integer, intent(out) :: count
    real, allocatable :: fresh_savg(:)
    logical(kind=1) :: search30
    allocate(fresh_savg(nfft))
    fresh_savg=savg
    search30=.false.
    count=-1
    call getcand2(ss,fresh_savg,2,1,390,10,10,0,selected_bin*df/1000.0, &
         search30,candidates,count)
  end subroutine search_selected

  logical function has_candidate(candidates,count,bin,period,sequence)
    type(candidate), intent(in) :: candidates(50)
    integer, intent(in) :: count,bin,period,sequence
    integer :: i
    has_candidate=.false.
    do i=1,count
       if(abs(candidates(i)%f-bin*df/1000.0)<df/2000.0 .and. &
            candidates(i)%ntrperiod==period .and. candidates(i)%iseq==sequence) has_candidate=.true.
    enddo
  end function has_candidate

  subroutine expect_candidate(value,bin,period,sequence,lag)
    type(candidate), intent(in) :: value
    integer, intent(in) :: bin,period,sequence,lag
    call require(abs(value%f-bin*df/1000.0)<df/2000.0,'candidate frequency')
    call require(abs(value%xdt-(0.15*lag-1.0))<0.001,'candidate time offset')
    call require(value%ntrperiod==period,'candidate period')
    call require(value%iseq==sequence,'candidate sequence')
  end subroutine expect_candidate

  subroutine same_candidate(a,b)
    type(candidate), intent(in) :: a,b
    call require(a%f==b%f .and. a%xdt==b%xdt .and. a%snr==b%snr .and. &
         a%ntrperiod==b%ntrperiod .and. a%iseq==b%iseq,'A-B-A candidate fields are stable')
  end subroutine same_candidate

  subroutine require(condition,description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description
    if(.not.condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    endif
  end subroutine require
end program test_qmap_candidates
