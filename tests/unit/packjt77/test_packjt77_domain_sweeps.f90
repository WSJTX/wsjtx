program test_packjt77_domain_sweeps
  use packjt77_grammar
  use packjt77_test_helpers, only: assert_true
  implicit none

  integer, parameter :: GRID4_TOTAL=18*18*10*10
  integer, parameter :: GRID6_TOTAL=18*18*10*10*24*24
  integer, parameter :: GRID6_WSPR_TOTAL=18*18*10*10*25*25
  integer :: nchecked

  nchecked=0
  call sweep_arrl_sections()
  call sweep_rtty_multipliers()
  call sweep_rtty_reports()
  call sweep_snr_reports()
  call sweep_qso_tails()
  call sweep_dxpedition_reports()
  call sweep_wspr_dbm()
  call sweep_field_day_exchanges()
  call sweep_wspr_affixes()
  call sweep_grid4()
  call sweep_grid6_samples()

  print '(a,i0,a)', 'test_packjt77_domain_sweeps: ', nchecked, &
       ' values enumerated'

contains

  subroutine sweep_arrl_sections()
    character(len=3) :: section
    integer :: i

    call assert_true('ARRL invalid text', &
         pack77_arrl_section_index('ZZZ').eq.-1)
    do i=1,PACK77_NSEC
       section=pack77_arrl_section_name(i)
       call assert_true('ARRL inverse', pack77_arrl_section_index(section).eq.i)
       if(section(1:1).ge.'A' .and. section(1:1).le.'Z') then
          section(1:1)=achar(iachar(section(1:1))+32)
          call assert_true('ARRL lowercase reject', &
               pack77_arrl_section_index(section).eq.-1)
       endif
       nchecked=nchecked+1
    enddo
    call assert_true('ARRL low index', pack77_arrl_section_name(0).eq.'   ')
    call assert_true('ARRL high index', &
         pack77_arrl_section_name(PACK77_NSEC+1).eq.'   ')
  end subroutine sweep_arrl_sections

  subroutine sweep_rtty_multipliers()
    character(len=3) :: multiplier
    integer :: i

    call assert_true('RTTY multiplier invalid text', &
         pack77_rtty_multiplier_index('ZZZ').eq.-1)
    do i=1,PACK77_NUSCAN
       multiplier=pack77_rtty_multiplier_name(i)
       call assert_true('RTTY multiplier inverse', &
            pack77_rtty_multiplier_index(multiplier).eq.i)
       if(multiplier(1:1).ge.'A' .and. multiplier(1:1).le.'Z') then
          multiplier(1:1)=achar(iachar(multiplier(1:1))+32)
          call assert_true('RTTY multiplier lowercase reject', &
               pack77_rtty_multiplier_index(multiplier).eq.-1)
       endif
       nchecked=nchecked+1
    enddo
    call assert_true('RTTY multiplier low index', &
         pack77_rtty_multiplier_name(0).eq.'   ')
    call assert_true('RTTY multiplier high index', &
         pack77_rtty_multiplier_name(PACK77_NUSCAN+1).eq.'   ')
  end subroutine sweep_rtty_multipliers

  subroutine sweep_rtty_reports()
    character(len=3) :: report
    integer :: i

    do i=0,7
       report=pack77_format_rtty_report(i)
       call assert_true('RTTY report inverse', &
            pack77_rtty_report_index(report).eq.i)
       nchecked=nchecked+1
    enddo
    call assert_true('RTTY report low reject', &
         pack77_rtty_report_index('519').eq.-1)
    call assert_true('RTTY report high reject', &
         pack77_rtty_report_index('609').eq.-1)
    call assert_true('RTTY report wrong length reject', &
         pack77_rtty_report_index('5799').eq.-1)
  end subroutine sweep_rtty_reports

  subroutine sweep_snr_reports()
    character(len=3) :: report
    integer :: isnr,irpt

    do isnr=-50,50
       irpt=pack77_report_index_from_snr(isnr)
       call assert_true('SNR report accepts legal value', irpt.ge.0)
       call assert_true('SNR report inverse', &
            pack77_snr_from_report_index(irpt).eq.isnr)
       report=pack77_format_snr_report(isnr)
       call assert_true('SNR report format width', len_trim(report).eq.3)
       nchecked=nchecked+1
    enddo
    do isnr=-75,-51
       call assert_true('SNR report low reject', &
            pack77_report_index_from_snr(isnr).eq.-1)
    enddo
    do isnr=51,75
       call assert_true('SNR report high reject', &
            pack77_report_index_from_snr(isnr).eq.-1)
    enddo
  end subroutine sweep_snr_reports

  subroutine sweep_qso_tails()
    character(len=4), parameter :: tails(3)=(/'RRR ','RR73','73  '/)
    integer :: i,irpt,nrpt

    do i=1,3
       call assert_true('QSO tail index', pack77_qso_tail_index(tails(i)).eq.i)
       irpt=pack77_type12_irpt_from_tail(i)
       call assert_true('Type 1/2 tail inverse', &
            pack77_tail_from_type12_irpt(irpt).eq.i)
       nrpt=pack77_type4_nrpt_from_tail(i)
       call assert_true('Type 4 tail inverse', &
            pack77_tail_from_type4_nrpt(nrpt).eq.i)
       nchecked=nchecked+1
    enddo
    call assert_true('QSO tail near miss', pack77_qso_tail_index('RR').eq.0)
    call assert_true('Type 1/2 tail low', pack77_tail_from_type12_irpt(1).eq.0)
    call assert_true('Type 4 tail high', pack77_tail_from_type4_nrpt(4).eq.0)
  end subroutine sweep_qso_tails

  subroutine sweep_dxpedition_reports()
    character(len=3) :: report
    integer :: isnr,n5
    logical :: ok

    do isnr=-31,33
       call format_signed_report(isnr,report)
       call pack77_parse_dxpedition_report(report,n5,ok)
       if(isnr.ge.-30 .and. isnr.le.32 .and. mod(isnr,2).eq.0) then
          call assert_true('DXpedition report accepts legal value', ok)
          call assert_true('DXpedition report inverse', 2*n5-30.eq.isnr)
          nchecked=nchecked+1
       else
          call assert_true('DXpedition report rejects illegal value', .not.ok)
       endif
    enddo
  end subroutine sweep_dxpedition_reports

  subroutine sweep_wspr_dbm()
    character(len=3) :: token
    integer :: dbm,idbm,decoded
    logical :: ok

    do dbm=-5,65
       write(token,'(i0)') dbm
       call pack77_parse_wspr_dbm(token,idbm,ok)
       if(ok) then
          decoded=nint(idbm*10.0/3.0)
          call assert_true('WSPR dBm inverse', decoded.eq.dbm)
          nchecked=nchecked+1
       else if(dbm.ge.0 .and. dbm.le.60) then
          call assert_true('WSPR dBm legal codebook only', &
               nint(nint(0.3*dbm)*10.0/3.0).ne.dbm)
       endif
    enddo
    call pack77_parse_wspr_dbm('03',idbm,ok)
    call assert_true('WSPR dBm leading zero reject', .not.ok)
  end subroutine sweep_wspr_dbm

  subroutine sweep_field_day_exchanges()
    character(len=4) :: token
    integer :: ntx,nclass,parsed_ntx,parsed_class
    logical :: ok

    do ntx=1,32
       do nclass=0,5
          write(token,'(i0,a1)') ntx, achar(iachar('A')+nclass)
          call pack77_parse_field_day_exchange(token,parsed_ntx,parsed_class,ok)
          call assert_true('Field Day exchange accepts legal value', ok)
          call assert_true('Field Day exchange inverse', &
               parsed_ntx.eq.ntx .and. parsed_class.eq.nclass)
          nchecked=nchecked+1
       enddo
    enddo
    call pack77_parse_field_day_exchange('0A',parsed_ntx,parsed_class,ok)
    call assert_true('Field Day ntx low reject', .not.ok)
    call pack77_parse_field_day_exchange('33A',parsed_ntx,parsed_class,ok)
    call assert_true('Field Day ntx high reject', .not.ok)
    call pack77_parse_field_day_exchange('1G',parsed_ntx,parsed_class,ok)
    call assert_true('Field Day class reject', .not.ok)
    call pack77_parse_field_day_exchange('01A',parsed_ntx,parsed_class,ok)
    call assert_true('Field Day leading zero reject', .not.ok)
  end subroutine sweep_field_day_exchanges

  subroutine sweep_wspr_affixes()
    character(len=13) :: token
    character(len=3) :: affix,affix_text
    integer :: i,npfx,slash_index
    logical :: is_prefix,ok

    do i=0,PACK77_WSPR_NZZZ+12959
       call pack77_wspr_affix_text(i,affix,is_prefix,ok)
       call assert_true('WSPR affix index renders', ok)
       affix_text=adjustl(affix)
       if(is_prefix) then
          token=trim(affix_text)//'/K1ABC'
          slash_index=len_trim(affix_text)+1
       else
          token='K1ABC/'//trim(affix_text)
          slash_index=6
       endif
       npfx=pack77_wspr_affix_index(token,slash_index,len_trim(token),ok)
       call assert_true('WSPR affix index inverse', ok .and. npfx.eq.i)
       call require_wspr_source(token//' 37', i)
       nchecked=nchecked+1
    enddo

    token='ABCD/K1ABC'
    npfx=pack77_wspr_affix_index(token,5,len_trim(token),ok)
    call assert_true('WSPR four-character prefix reject', .not.ok)
    token='K1ABC/ABA'
    npfx=pack77_wspr_affix_index(token,6,len_trim(token),ok)
    call assert_true('WSPR suffix non-digit third reject', .not.ok)
    call require_wspr_source_reject('00/K1ABC 37')
    call require_wspr_source_reject('K1ABC/00 37')
  end subroutine sweep_wspr_affixes

  subroutine require_wspr_source(msg,want_npfx)
    character(len=*), intent(in) :: msg
    integer, intent(in) :: want_npfx
    type(pack77_wspr_source) :: source
    logical :: ok,matched_shape

    call pack77_parse_wspr_source(msg,source,ok,matched_shape)
    call assert_true('WSPR source affix parse', &
         ok .and. source%npfx.eq.want_npfx)
  end subroutine require_wspr_source

  subroutine require_wspr_source_reject(msg)
    character(len=*), intent(in) :: msg
    type(pack77_wspr_source) :: source
    logical :: ok,matched_shape

    call pack77_parse_wspr_source(msg,source,ok,matched_shape)
    call assert_true('WSPR source alias reject '//trim(msg), .not.ok)
  end subroutine require_wspr_source_reject

  subroutine sweep_grid4()
    character(len=4) :: grid4
    integer :: i,narg
    logical :: ok

    do i=0,GRID4_TOTAL-1
       narg=i
       call to_grid4(narg,grid4,ok)
       call assert_true('Grid4 renders', ok)
       call assert_true('Grid4 validates', pack77_is_grid4(grid4))
       call assert_true('Grid4 inverse', pack77_grid4_index(grid4).eq.i)
       nchecked=nchecked+1
    enddo
    narg=-1
    call to_grid4(narg,grid4,ok)
    call assert_true('Grid4 low index reject', .not.ok)
    narg=GRID4_TOTAL
    call to_grid4(narg,grid4,ok)
    call assert_true('Grid4 high index reject', .not.ok)
    call assert_true('Grid4 short reject', .not.pack77_is_grid4('AA0'))
    call assert_true('Grid4 lowercase reject', .not.pack77_is_grid4('aa00'))
  end subroutine sweep_grid4

  subroutine sweep_grid6_samples()
    integer, parameter :: NSAMPLE=10000
    integer(kind=8) :: state
    integer :: i,n

    call check_grid6_index(0)
    call check_grid6_index(GRID6_TOTAL-1)
    call check_grid6_index(18*10*10*24*24-1)
    call check_grid6_index(18*10*10*24*24)
    call check_wspr_grid_index(0)
    call check_wspr_grid_index(GRID6_WSPR_TOTAL-1)
    call check_wspr_grid_index(18*10*10*25*25-1)
    call check_wspr_grid_index(18*10*10*25*25)

    state=123456789_8
    do i=1,NSAMPLE
       state=mod(1103515245_8*state+12345_8,2147483648_8)
       n=int(mod(state,int(GRID6_TOTAL,kind=8)))
       call check_grid6_index(n)
       state=mod(1103515245_8*state+12345_8,2147483648_8)
       n=int(mod(state,int(GRID6_WSPR_TOTAL,kind=8)))
       call check_wspr_grid_index(n)
    enddo
  end subroutine sweep_grid6_samples

  subroutine check_grid6_index(n)
    integer, intent(in) :: n
    character(len=6) :: grid6
    integer :: narg
    logical :: ok

    narg=n
    call to_grid6(narg,grid6,ok)
    call assert_true('Grid6 renders', ok)
    call assert_true('Grid6 validates', pack77_is_grid6(grid6,.false.))
    call assert_true('Grid6 inverse', pack77_grid6_index(grid6).eq.n)
    nchecked=nchecked+1
  end subroutine check_grid6_index

  subroutine check_wspr_grid_index(n)
    integer, intent(in) :: n
    character(len=6) :: grid6
    integer :: narg
    logical :: ok

    narg=n
    call to_grid(narg,grid6,ok)
    call assert_true('WSPR Grid6 renders', ok)
    call assert_true('WSPR Grid6 inverse', pack77_grid6_wspr_index(grid6).eq.n)
    nchecked=nchecked+1
  end subroutine check_wspr_grid_index

  subroutine format_signed_report(value,token)
    integer, intent(in) :: value
    character(len=3), intent(out) :: token

    write(token,'(sp,i3.2)') value
    if(value.ge.0 .and. token(1:1).eq.' ') token(1:1)='+'
  end subroutine format_signed_report

end program test_packjt77_domain_sweeps
