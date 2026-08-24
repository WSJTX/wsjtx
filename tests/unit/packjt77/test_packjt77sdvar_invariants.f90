program test_packjt77sdvar_invariants

  use packjt77
  use packjt77_test_helpers
  use ft8_mod1, only : idtone56_valid,idtone56,itone56,msg,lastrxmsg,mycall, &
       hiscall,nlasttx,csyncsd_valid,csyncsdcq_valid
  implicit none

  integer :: ntests
  external :: genft8sdvar
  external :: ft8sdvar
  external :: ft8svar
  external :: tonesdvar

  ntests=0

  call expect_genft8sdvar_accepts_type1_template()
  call expect_genft8sdvar_requires_expanded_dx_macro()
  call expect_genft8sdvar_rejects_free_text_template()
  call expect_invalid_sd_template_is_not_scored()
  call expect_ft8svar_last_grid_template_is_valid()
  call expect_failed_qso_template_invalidates_sync()
  call expect_failed_cq_template_invalidates_sync()
  call expect_genft8sdvar_rejects_hash_call_without_recording_hashes()

  write(*,1000) ntests
1000 format('packjt77 unified SD-var invariant tests passed: ',i0)

contains

  subroutine expect_genft8sdvar_accepts_type1_template()
    character(len=37) :: input, msgsent
    integer :: i3, n3
    integer :: itone(79)
    integer*1 :: msgbits(77)

    input='K1ABC W9XYZ RR73'
    msgsent='                                     '
    msgbits=0
    itone=0
    i3=-1
    n3=-1

    call genft8sdvar(input,i3,n3,msgsent,msgbits,itone)

    call assert_int('sdvar Type 1 template i3',1,i3)
    call assert_int('sdvar Type 1 template n3',0,n3)
    call assert_text_equal('sdvar Type 1 template decode','K1ABC W9XYZ RR73',msgsent)
    call assert_true('sdvar Type 1 template message bits',any(msgbits.ne.0))
    call assert_true('sdvar Type 1 template tones',any(itone.ne.0))

    ntests=ntests+1
  end subroutine expect_genft8sdvar_accepts_type1_template

  subroutine expect_genft8sdvar_requires_expanded_dx_macro()
    character(len=37) :: input, direct, msgsent
    integer :: i3, n3
    integer :: itone(79)
    integer*1 :: msgbits(77)

    input='$DX K1ABC FN42'
    direct='W9XYZ K1ABC FN42'
    msgsent='                                     '
    msgbits=0
    itone=0
    i3=-1
    n3=-1

    call genft8sdvar(input,i3,n3,msgsent,msgbits,itone)

    call assert_int('sdvar unexpanded DX macro i3',-1,i3)
    call assert_int('sdvar unexpanded DX macro n3',-1,n3)
    call assert_text_equal('sdvar unexpanded DX macro message', &
         '*** bad message ***',msgsent)
    call assert_true('sdvar unexpanded DX macro message bits', &
         all(msgbits.eq.0))
    call assert_true('sdvar unexpanded DX macro tones',all(itone.eq.0))

    call genft8sdvar(direct,i3,n3,msgsent,msgbits,itone)

    call assert_int('sdvar pre-expanded DX macro i3',1,i3)
    call assert_int('sdvar pre-expanded DX macro n3',0,n3)
    call assert_text_equal('sdvar pre-expanded DX macro decode',direct, &
         msgsent)
    call assert_true('sdvar pre-expanded DX macro message bits', &
         any(msgbits.ne.0))
    call assert_true('sdvar pre-expanded DX macro tones',any(itone.ne.0))

    ntests=ntests+1
  end subroutine expect_genft8sdvar_requires_expanded_dx_macro

  subroutine expect_invalid_sd_template_is_not_scored()
    real :: s8(0:7,79)
    integer :: itone(79)
    character(len=37) :: msgd
    character(len=37) :: msg37
    logical(1) :: lft8sd, lcq

    msgd='CQ PJ4/K1ABC'
    s8=0.0
    s8(0,:)=1.0
    itone=-1
    msg37=''
    lft8sd=.false.
    lcq=.true.

    call ft8sdvar(s8,0.0,itone,msgd,msg37,lft8sd,lcq)

    call assert_true('invalid SD template not scored',.not.logical(lft8sd))
    call assert_text_equal('invalid SD template message unchanged','',msg37)

    ntests=ntests+1
  end subroutine expect_invalid_sd_template_is_not_scored

  subroutine expect_ft8svar_last_grid_template_is_valid()
    real :: s8(0:7,79)
    integer :: itone(79)
    character(len=37) :: decoded
    logical(1) :: lft8s

    call clear_all_state('','')

    mycall='N0AAA'
    hiscall='W9XYZ'
    nlasttx=1
    msg(53)='N0AAA W9XYZ AA00'
    lastrxmsg(1)%lstate=.true.
    lastrxmsg(1)%lastmsg='N0AAA W9XYZ FN42'
    idtone56(53,1:58)=0
    itone56(53,1:79)=0
    idtone56_valid(53)=.false.
    s8=0.0
    itone=0
    decoded=''
    lft8s=.false.

    call ft8svar(s8,0.0,itone,decoded,lft8s,1,.false.)

    call assert_true('ft8svar last grid row valid',logical(idtone56_valid(53)))
    call assert_text_equal('ft8svar last grid message', &
         'N0AAA W9XYZ FN42',msg(53))
    call assert_true('ft8svar last grid tones',any(itone56(53,1:79).ne.0))

    ntests=ntests+1
  end subroutine expect_ft8svar_last_grid_template_is_valid

  subroutine expect_failed_qso_template_invalidates_sync()
    character(len=37) :: msgd

    csyncsd_valid=.true.
    msgd='<PJ4/K1ABC> W9XYZ -01'

    call tonesdvar(msgd,.false.)

    call assert_true('failed QSO template invalidates sync', &
         .not.logical(csyncsd_valid))

    ntests=ntests+1
  end subroutine expect_failed_qso_template_invalidates_sync

  subroutine expect_failed_cq_template_invalidates_sync()
    character(len=37) :: msgd

    csyncsdcq_valid=.true.
    msgd='CQ PJ4/K1ABC'

    call tonesdvar(msgd,.true.)

    call assert_true('failed CQ template invalidates sync', &
         .not.logical(csyncsdcq_valid))

    ntests=ntests+1
  end subroutine expect_failed_cq_template_invalidates_sync

  subroutine expect_genft8sdvar_rejects_free_text_template()
    character(len=37) :: input, msgsent
    integer :: i3, n3
    integer :: itone(79)
    integer*1 :: msgbits(77)

    input='FREE TEXT MSG'
    msgsent='                                     '
    msgbits=1
    itone=1
    i3=-1
    n3=-1

    call genft8sdvar(input,i3,n3,msgsent,msgbits,itone)

    call assert_bad_sd_template('sdvar free-text template',i3,n3,msgsent,msgbits,itone)

    ntests=ntests+1
  end subroutine expect_genft8sdvar_rejects_free_text_template

  subroutine expect_genft8sdvar_rejects_hash_call_without_recording_hashes()
    character(len=37) :: input, msgsent
    integer :: i3, n3, itone(79)
    integer*1 :: msgbits(77)

    call clear_all_state('','')

    input='<PJ4/K1ABC> W9XYZ RR73'
    msgsent='                                     '
    msgbits=0
    itone=0
    i3=-1
    n3=-1

    call genft8sdvar(input,i3,n3,msgsent,msgbits,itone)

    call assert_bad_sd_template('sdvar hash-call template',i3,n3,msgsent,msgbits,itone)
    call assert_int('sdvar generator does not record nzhash',0,nzhash)
    call assert_true('sdvar generator leaves calls10 empty',all(calls10.eq.''))
    call assert_true('sdvar generator leaves calls12 empty',all(calls12.eq.''))
    call assert_true('sdvar generator leaves calls22 empty',all(calls22.eq.''))
    call assert_true('sdvar generator leaves ihash22 empty',all(ihash22.eq.-1))

    ntests=ntests+1
  end subroutine expect_genft8sdvar_rejects_hash_call_without_recording_hashes

  subroutine assert_bad_sd_template(label,i3,n3,msgsent,msgbits,itone)
    character(len=*), intent(in) :: label
    integer, intent(in) :: i3, n3, itone(79)
    integer*1, intent(in) :: msgbits(77)
    character(len=37), intent(in) :: msgsent

    call assert_int(trim(label)//' rejected i3',-1,i3)
    call assert_int(trim(label)//' rejected n3',-1,n3)
    call assert_text_equal(trim(label)//' rejected message','*** bad message ***',msgsent)
    call assert_true(trim(label)//' zero message bits',all(msgbits.eq.0))
    call assert_true(trim(label)//' zero tones',all(itone.eq.0))
  end subroutine assert_bad_sd_template

end program test_packjt77sdvar_invariants
