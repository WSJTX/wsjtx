program test_ft8_callsign_database
  use jt65_mod9, only: load_callsign_database, callsign_database_available, callsign_in_database
  implicit none
  character(len=*), parameter :: fixture='test-callsign-database.txt'
  character(len=16), parameter :: prefixes='ADEGIJKLNOPRSUWX'
  integer, parameter :: old_capacity(16)=[11500,11100,9000,7500,7600,10000,24000,6500, &
                                         9400,6700,5800,6500,7500,10400,13700,5800]
  character(len=7), parameter :: shipped_hits(16)=[character(len=7) :: &
    '2D0OMN','D2ACE','E20AX','G0ABI','I0ABU','J35AC','K0ACP','L22D', &
    'N0A','OA1F','P29CW','R0AAW','S01WS','U1BD','W0A','XE1ACA']
  character(len=1024) :: shipped_path
  character(len=12) :: first,second
  integer :: nfail,unit,i,j
  logical(1) :: found

  nfail=0
  call expect(.not.callsign_database_available(),'database starts unavailable')
  call check_search('K9ZZZ','','',.true.)

  open(newunit=unit,file=fixture,status='replace',action='write')
  do i=1,len(prefixes)
    do j=1,old_capacity(i)+1
      write(unit,'(a)') prefixes(i:i)//'1AAA,AA00'
    enddo
    write(unit,'(a)') prefixes(i:i)//'9ZZZ,AA00'
  enddo
  close(unit)
  call load_callsign_database(fixture)
  call expect(callsign_database_available(),'oversized buckets are available')
  do i=1,len(prefixes)
    call expect(callsign_in_database(prefixes(i:i)//'1AAA'),'oversized bucket first entry')
    call expect(callsign_in_database(prefixes(i:i)//'9ZZZ'),'oversized bucket final entry')
    call expect(.not.callsign_in_database(prefixes(i:i)//'9ZZY'),'oversized bucket miss')
  enddo

  open(newunit=unit,file=fixture,status='replace',action='write')
  do i=1,200001
    write(unit,'(a)') '// ignored physical line'
  enddo
  call write_complete(unit)
  write(unit,'(a)') '', '//K9COM,AA00', 'K1,AA00', 'K1234567,AA00', 'k9LOW,AA00', &
    '0AA,AA00', ' K9PAD,AA00', 'K9NOCOMMA', '1AA,AA00', 'K1AB,AA00', 'K1ABC,AA00', &
    'K1ABCD,AA00', 'K1ABCDE,AA00', 'ZZZY,AA00'
  close(unit)
  call load_callsign_database(fixture)
  call expect(callsign_database_available(),'complete records beyond physical line 200000 at EOF')
  call expect(.not.callsign_in_database('A9ZZZ'),'a successful load replaces the old database')
  call expect(callsign_in_database('1AA'),'three-character record')
  call expect(callsign_in_database('K1AB'),'four-character record')
  call expect(callsign_in_database('K1ABC'),'five-character record')
  call expect(callsign_in_database('K1ABCD'),'six-character record')
  call expect(callsign_in_database('K1ABCDE'),'seven-character record')
  call expect(callsign_in_database('ZZZY'),'last valid prefix before sentinel')
  call expect(.not.callsign_in_database('K9COM'),'comment ignored')
  call expect(.not.callsign_in_database('K1'),'short record ignored')
  call expect(.not.callsign_in_database('K1234567'),'long record ignored')
  call expect(.not.callsign_in_database('k9LOW'),'lowercase remains unaccepted')
  call expect(.not.callsign_in_database('0AA'),'zero prefix remains unaccepted')
  call expect(.not.callsign_in_database(' K9PAD'),'leading whitespace remains unaccepted')
  call expect(.not.callsign_in_database('K9NOCOMMA'),'record without comma ignored')
  call expect(.not.callsign_in_database(''),'empty call misses')

  call check_search('A1AAA','','',.true.)
  call check_search('K9ZZZ','D1AAA','D1AAA',.true.)
  call check_search('TU;','D1AAA','D1AAA',.true.)
  call check_search('TU;','','',.false.)
  call check_search('K9ZZZ','D9ZZZ','D9ZZZ',.false.)
  call check_search('K9ZZZ','','',.false.)
  call check_search('SPECIALCALL','','',.true.)
  call check_search('SPECIALCALL','D1AAA','D1AAA',.true.)
  call check_search('SPECIALCALL','D9ZZZ','D9ZZZ',.false.)
  call check_search('K9ZZZ','SPECIALCALL','',.false.)
  call check_search('A1AAA','SPECIALCALL','',.true.)
  first='K9ZZZ'
  second=''
  found=.true.
  call searchcallsvar(first,second,found)
  call expect(logical(found),'a lookup miss preserves its incoming result')

  open(newunit=unit,file=fixture,status='replace',action='write')
  call write_complete(unit)
  write(unit,'(a)') 'ZZZZ,AA00', 'K9END,AA00'
  close(unit)
  call load_callsign_database(fixture)
  call expect(callsign_database_available(),'sentinel terminates a complete database')
  call expect(.not.callsign_in_database('K9END'),'records beyond sentinel ignored')
  call expect(.not.callsign_in_database('ZZZZ'),'sentinel is not a record')
  call expect(.not.callsign_in_database('K1ABCDE'),'replacement does not retain previous records')

  open(newunit=unit,file=fixture,status='replace',action='write')
  call write_complete(unit,15)
  close(unit)
  call load_callsign_database(fixture)
  call check_unavailable('incomplete bucket coverage')
  open(newunit=unit,file=fixture,status='old')
  close(unit,status='delete')
  call load_callsign_database(fixture)
  call check_unavailable('missing file after incomplete load')

  open(newunit=unit,file=fixture,status='replace',action='write')
  call write_complete(unit)
  write(unit,'(a)') 'ZZZZ'
  close(unit)
  call load_callsign_database(fixture)
  call expect(callsign_database_available(),'successful load after failures')
  call load_callsign_database('.')
  call check_unavailable('directory read or open failure after success')
  call load_callsign_database(fixture)
  call expect(callsign_database_available(),'successful load after read failure')
  open(newunit=unit,file=fixture,status='old')
  close(unit,status='delete')
  call load_callsign_database(fixture)
  call check_unavailable('missing file after success')

  call get_command_argument(1,shipped_path)
  call expect(len_trim(shipped_path).gt.0,'shipped database path supplied')
  call load_callsign_database(trim(shipped_path))
  call expect(callsign_database_available(),'shipped database is complete')
  do i=1,size(shipped_hits)
    call expect(callsign_in_database(shipped_hits(i)),'shipped hit '//trim(shipped_hits(i)))
    call check_search(shipped_hits(i),'','',.true.)
    call expect(.not.callsign_in_database(prefixes(i:i)//'??????'),'shipped bucket miss')
  enddo

  if(nfail.ne.0) then
    print *, 'FT8 callsign database tests FAILED:',nfail
    error stop 1
  endif
  print *, 'FT8 callsign database tests passed'

contains

  subroutine write_complete(output,limit)
    integer, intent(in) :: output
    integer, optional, intent(in) :: limit
    integer :: bucket,n
    n=len(prefixes)
    if(present(limit)) n=limit
    do bucket=1,n
      write(output,'(a)') prefixes(bucket:bucket)//'1AAA,AA00'
    enddo
  end subroutine write_complete

  subroutine check_search(call1,call2,remaining,wanted)
    character(len=*), intent(in) :: call1,call2,remaining
    logical, intent(in) :: wanted
    character(len=12) :: c1,c2
    logical(1) :: result
    c1=call1
    c2=call2
    result=.false.
    call searchcallsvar(c1,c2,result)
    call expect(logical(result).eqv.wanted,'search '//trim(call1)//' '//trim(call2))
    call expect(c2.eq.remaining,'second-call special handling')
  end subroutine check_search

  subroutine check_unavailable(reason)
    character(len=*), intent(in) :: reason
    call expect(.not.callsign_database_available(),reason)
    call expect(.not.callsign_in_database('A1AAA'),'failed load publishes no database')
    call check_search('K9ZZZ','','',.true.)
  end subroutine check_unavailable

  subroutine expect(condition,description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description
    if(condition) return
    print *, 'FAIL: ',description
    nfail=nfail+1
  end subroutine expect

end program test_ft8_callsign_database
