  program test_packjt77_mutation_sweep
  use packjt77
  use packjt77_test_helpers
  implicit none

  integer, parameter :: MAX_SEEDS=400,MAX_MUTANTS=50000
  character(len=37) :: seeds(MAX_SEEDS)
  integer :: nseeds,nmutants,nexact,ncanon,nreject

  nseeds=0
  nmutants=0
  nexact=0
  ncanon=0
  nreject=0

  call check_canonical_matcher()
  call generate_seeds()
  call assert_true('seed generator coverage', nseeds.ge.200)
  call sweep_mutations()

  print '(a,i0,a,i0,a,i0,a,i0)', &
       'test_packjt77_mutation_sweep: mutants=', nmutants, &
       ' encoded-exact=', nexact, ' encoded-canonical=', ncanon, &
       ' rejected=', nreject

contains

  subroutine check_canonical_matcher()
    call assert_true('canonical compound bracket', &
         canonical_match('W1AW/P','<W1AW/P>'))
    call assert_true('canonical compound placeholder', &
         canonical_match('PJ2/W1AW','<...>'))
    call assert_true('canonical bracket placeholder', &
         canonical_match('<PJ4/K1ABC>','<...>'))
    call assert_true('canonical grid bracket reject', &
         .not.canonical_match('FN42','<FN42>'))
    call assert_true('canonical trailing slash reject', &
         .not.canonical_match('K1ABC/','<K1ABC/>'))
    call assert_true('canonical leading slash reject', &
         .not.canonical_match('/K1ABC','</K1ABC>'))
    call assert_true('canonical double slash reject', &
         .not.canonical_match('A//B','<A//B>'))
  end subroutine check_canonical_matcher

  subroutine generate_seeds()
    character(len=13), parameter :: calls(6)=(/'K1ABC        ', &
         'W9XYZ        ','KA1ABC       ','WA9XYZ       ','PJ2/W1AW     ', &
         'W1AW/P       '/)
    character(len=4), parameter :: grids(4)=(/'FN42','EM10','DM79','IO91'/)
    character(len=3), parameter :: snrs(4)=(/'-12','+00','+20','R-7'/)
    character(len=3), parameter :: rtty_reports(4)=(/'529','559','579','599'/)
    character(len=3), parameter :: mults(4)=(/'MA ','CT ','DX ','EMA'/)
    character(len=6), parameter :: vhf_exch(4)=(/'520001','590001', &
         '592047','582047'/)
    integer :: i,j
    character(len=13) :: free_seed

    do i=1,4
       do j=1,4
          call add_seed(trim(calls(i))//' '//trim(calls(j))//' '//grids(j))
          call add_seed(trim(calls(i))//' '//trim(calls(j))//' '//snrs(i))
          call add_seed(trim(calls(i))//' '//trim(calls(j))//' R '//grids(j))
          call add_seed(trim(calls(i))//' '//trim(calls(j))//' RRR')
       enddo
    enddo

    do i=1,4
       call add_seed(trim(calls(i))//' RR73; W9XYZ <KH1/KH7Z> -12')
       call add_seed(trim(calls(5))//' RR73; W9XYZ <KH1/KH7Z> -12')
       call add_seed(trim(calls(i))//' '//trim(calls(j_index(i)))//' R 1A EMA')
       call add_seed(trim(calls(6))//' '//trim(calls(i))//' 5A CT')
       call add_seed('TU; '//trim(calls(i))//' W9XYZ R '//rtty_reports(i)// &
            ' '//trim(mults(i)))
       call add_seed(trim(calls(i))//' W9XYZ '//rtty_reports(i)// &
            ' '//trim(mults(i)))
       call add_seed('<PJ4/K1ABC> '//trim(calls(i))//' RR73')
       call add_seed(trim(calls(5))//' <W7ABC> RR73')
       call add_seed('<W3CCX> <K1JT/P> '//vhf_exch(i)//' FN20QI')
       call add_seed('<W3CCX> <K1JT/P> R '//vhf_exch(i)//' FN20QI')
       call add_seed(trim(calls(i))//' '//grids(i)//' 37')
       call add_seed('0/'//trim(calls(i))//' 37')
       call add_seed(trim(calls(i))//'/1 37')
    enddo

    call add_seed('CQ PJ2/W1AW')
    call add_seed('CQ K1ABC FN42')
    call add_seed('123456789ABCDEF012')
    call add_seed('0000000000000000DE')
    call add_seed('HELLO WORLD 73')
    call add_seed('FREE TEXT MSG')

    do i=1,80
       call add_seed('K1ABC W9XYZ +'//digit2(mod(i,50)))
       call add_seed('K1ABC W9XYZ R+'//digit2(mod(i,50)))
       call add_seed('WA9XYZ KA1ABC '//field_day_exchange(i))
    enddo

    do i=1,160
       write(free_seed,'("TXT",i3.3," MSG")') i
       call add_seed(free_seed)
    enddo
  end subroutine generate_seeds

  integer function j_index(i) result(j)
    integer, intent(in) :: i

    j=mod(i,4)+1
  end function j_index

  character(len=2) function digit2(n) result(text)
    integer, intent(in) :: n

    write(text,'(i2.2)') n
  end function digit2

  character(len=3) function field_day_exchange(i) result(text)
    integer, intent(in) :: i

    write(text,'(i0,a1)') mod(i-1,32)+1, achar(iachar('A')+mod(i,6))
  end function field_day_exchange

  subroutine add_seed(text)
    character(len=*), intent(in) :: text
    character(len=37) :: msg,decoded
    logical :: ok
    type(pack77_result) :: encoded

    if(nseeds.ge.MAX_SEEDS) return
    msg='                                     '
    msg=text
    call clear_all_state('N0AAA','N0BBB')
    encoded=pack77_result_from_api(msg)
    if(.not.encoded%encoded) return
    decoded='                                     '
    ok=.false.
    call unpack77(encoded%c77,0,decoded,ok)
    call assert_true('seed decodes', ok)
    nseeds=nseeds+1
    seeds(nseeds)=msg
  end subroutine add_seed

  subroutine sweep_mutations()
    integer :: i

    do i=1,nseeds
       call mutate_seed(seeds(i))
       if(nmutants.ge.MAX_MUTANTS) exit
    enddo
  end subroutine sweep_mutations

  subroutine mutate_seed(seed)
    character(len=*), intent(in) :: seed
    character(len=8), parameter :: probes='A0/<>+. '
    character(len=37) :: mutant
    integer :: i,j,n

    n=len_trim(seed)
    do i=1,n
       do j=1,len(probes)
          mutant=seed
          mutant(i:i)=probes(j:j)
          call check_mutant(mutant)
          if(nmutants.ge.MAX_MUTANTS) return
       enddo
    enddo

    do i=1,n
       mutant='                                     '
       if(i.gt.1) mutant(1:i-1)=seed(1:i-1)
       if(i.lt.n) mutant(i:n-1)=seed(i+1:n)
       call check_mutant(mutant)
       if(nmutants.ge.MAX_MUTANTS) return
    enddo

    call mutate_tokens(seed)
  end subroutine mutate_seed

  subroutine mutate_tokens(seed)
    character(len=*), intent(in) :: seed
    character(len=37) :: tokens(19),mutant
    integer :: ntokens,i,j

    call split_tokens(seed,tokens,ntokens)
    do i=1,ntokens
       mutant=join_tokens(tokens,ntokens,i,i)
       call check_mutant(mutant)
       if(nmutants.ge.MAX_MUTANTS) return
    enddo
    do i=1,ntokens-1
       do j=i+1,ntokens
          mutant=join_tokens(tokens,ntokens,i,j)
          call check_mutant(mutant)
          if(nmutants.ge.MAX_MUTANTS) return
       enddo
    enddo
  end subroutine mutate_tokens

  character(len=37) function join_tokens(tokens,ntokens,a,b) result(msg)
    character(len=37), intent(in) :: tokens(:)
    integer, intent(in) :: ntokens,a,b
    integer :: i
    character(len=37) :: work(19)

    work=tokens
    if(a.eq.b) then
       msg='                                     '
       do i=1,ntokens
          call append_token(msg,tokens(i))
          if(i.eq.a) call append_token(msg,tokens(i))
       enddo
    else
       work(a)=tokens(b)
       work(b)=tokens(a)
       msg='                                     '
       do i=1,ntokens
          call append_token(msg,work(i))
       enddo
    endif
  end function join_tokens

  subroutine append_token(msg,token)
    character(len=37), intent(inout) :: msg
    character(len=*), intent(in) :: token
    integer :: n,m

    n=len_trim(msg)
    m=len_trim(token)
    if(m.eq.0) return
    if(n.eq.0) then
       if(m.le.37) msg(1:m)=token(1:m)
    else if(n+m+1.le.37) then
       msg(n+1:n+1)=' '
       msg(n+2:n+m+1)=token(1:m)
    endif
  end subroutine append_token

  subroutine check_mutant(mutant)
    character(len=*), intent(in) :: mutant
    character(len=37) :: msg,decoded
    logical :: ok,is_exact,is_canonical,is_telemetry
    type(pack77_result) :: encoded

    if(nmutants.ge.MAX_MUTANTS) return
    nmutants=nmutants+1
    msg='                                     '
    msg=mutant
    call clear_all_state('N0AAA','N0BBB')
    encoded=pack77_result_from_api(msg)
    if(.not.encoded%encoded) then
       nreject=nreject+1
       return
    endif
    call assert_true('encoded gate rejected', encoded%status.ne.PACK77_STATUS_INTERNAL_ROUNDTRIP_REJECTED)
    decoded='                                     '
    ok=.false.
    call unpack77(encoded%c77,0,decoded,ok)
    if(.not.ok) then
       write(*,'(a,a,a,a,a)') 'silent rewrite: input="', trim(msg), &
            '" c77="', trim(encoded%c77), '"'
       error stop 1
    endif
    is_exact=normalized_equal(msg,decoded)
    is_canonical=canonical_match(msg,decoded)
    is_telemetry=telemetry_match(msg,decoded)
    if(.not.is_exact .and. .not.is_canonical .and. .not.is_telemetry) then
       write(*,'(a,a,a,a,a,a,a)') 'silent rewrite: input="', trim(msg), &
            '" decoded="', trim(decoded), '" c77="', trim(encoded%c77), '"'
       error stop 1
    endif
    if(is_exact) then
       nexact=nexact+1
    else if(is_telemetry) then
       ncanon=ncanon+1
    else
       call assert_true('canonical mutation decode', is_canonical)
       ncanon=ncanon+1
    endif
  end subroutine check_mutant

end program test_packjt77_mutation_sweep
