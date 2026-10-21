! SPDX-License-Identifier: GPL-3.0-or-later
program test_streaming_control
  use streaming_control, only: parse_control_frame, configure_fields,       &
       control_type_error, CTRL_UNKNOWN, CTRL_CONFIGURE, CTRL_HALT,         &
       CTRL_PARSE_ERR, CTRL_DISCONTINUITY, CTRL_PACK, CTRL_RENDER,          &
       MODE_JTTY, encode_request, parse_encode_request, parse_render_request
  implicit none

  integer :: nfail = 0
  character(len=12) :: code
  character(len=:), allocatable :: long_text

  call expect_action('{"t":"discontinuity"}', CTRL_DISCONTINUITY)
  call expect_action('{"t":"halt"}', CTRL_HALT)
  call expect_action('{"t":"configure","rxfreq":1500}', CTRL_CONFIGURE)
  call expect_action('{"t":"a_future_control"}', CTRL_UNKNOWN)
  call expect_action('{"t":"discontinuities"}', CTRL_UNKNOWN)
  call expect_action('{"t":"discontinuit"}', CTRL_UNKNOWN)
  call expect_action('{"x":"discontinuity"}', CTRL_PARSE_ERR)

  ! JTTY is selected by its name only, and only by "mode".
  call expect_mode('{"t":"configure","mode":"JTTY"}', .true., MODE_JTTY)
  call expect_mode('{"t":"configure","mode":"jtty"}', .true., MODE_JTTY)
  call expect_mode('{"t":"configure","mode":"FT8"}', .true., 8)
  write(code, '(i0)') MODE_JTTY
  call expect_mode('{"t":"configure","mode":' // trim(code) // '}', .false., 0)
  call expect_tx_mode_unset('{"t":"configure","tx_mode":"JTTY"}')

  ! String values decode JSON escapes to UTF-8 (RFC 8259 section 7, RFC 3629).
  call expect_call('{"t":"configure","mycall":"K1\/ABC"}', 'K1/ABC')
  call expect_call('{"t":"configure","mycall":"AB\\","mygrid":"EM18"}', 'AB\', 'EM18')
  call expect_call('{"t":"configure","mycall":"A\"B","mygrid":"EM18"}', 'A"B', 'EM18')
  call expect_call('{"t":"configure","mycall":"\b\f\n\r\t"}',                 &
       achar(8) // achar(12) // achar(10) // achar(13) // achar(9))
  call expect_call('{"t":"configure","mycall":"\u004B1ABC"}', 'K1ABC')
  call expect_call('{"t":"configure","mycall":"\u00e9"}', char(195) // char(169))
  call expect_call('{"t":"configure","mycall":"\u20AC"}',                     &
       char(226) // char(130) // char(172))
  call expect_call('{"t":"configure","mycall":"\ud83d\ude00"}',               &
       char(240) // char(159) // char(152) // char(128))
  call expect_call('{"t":"configure","mycall":"\uD83D\uDE00"}',               &
       char(240) // char(159) // char(152) // char(128))
  call expect_call('{"t":"configure","mycall":"A\u0000B"}', 'A' // achar(0) // 'B')
  ! The bounds of each UTF-8 length and of the surrogates, and every hex digit case.
  call expect_call('{"t":"configure","mycall":"\u0080"}', char(194) // char(128))
  call expect_call('{"t":"configure","mycall":"\u07ff"}', char(223) // char(191))
  call expect_call('{"t":"configure","mycall":"\u0800"}', char(224) // char(160) // char(128))
  call expect_call('{"t":"configure","mycall":"\uFFFF"}', char(239) // char(191) // char(191))
  call expect_call('{"t":"configure","mycall":"\uabcd"}', char(234) // char(175) // char(141))
  call expect_call('{"t":"configure","mycall":"\ud800\udc00"}',              &
       char(240) // char(144) // char(128) // char(128))
  call expect_call('{"t":"configure","mycall":"\uDBFF\uDFFF"}',              &
       char(244) // char(143) // char(191) // char(191))
  call expect_action('{"t":"ha\u006ct"}', CTRL_HALT)
  call expect_action('{"t":"ha\x"}', CTRL_PARSE_ERR)
  call expect_mode('{"t":"configure","mode":"JT\u0054Y"}', .true., MODE_JTTY)
  ! An escaped quote neither ends the value nor starts a key.
  call expect_call('{"t":"configure","mycall":"A\",\"mygrid\":\"XX","mygrid":"EM18"}', &
       'A","mygrid":', 'EM18')
  ! A string that does not decode is unreadable: the key stays unset.
  call expect_call('{"t":"configure","mycall":"A\x"}', '')
  call expect_call('{"t":"configure","mycall":"A\u12"}', '')
  call expect_call('{"t":"configure","mycall":"A\u12G4"}', '')
  call expect_call('{"t":"configure","mycall":"\ud83d"}', '')
  call expect_call('{"t":"configure","mycall":"\ude00"}', '')
  call expect_call('{"t":"configure","mycall":"\ud83dA"}', '')
  call expect_call('{"t":"configure","mycall":"\ud83d\u0041"}', '')
  call expect_call('{"t":"configure","mycall":"\ud83d\\de00"}', '')
  call expect_call('{"t":"configure","mycall":"\udfff"}', '')
  ! Frames that end inside an escape or a string.
  call expect_call('{"t":"configure","mycall":"A\u12', '')
  call expect_call('{"t":"configure","mycall":"\ud83d', '')
  call expect_call('{"t":"configure","mycall":"A\', '')
  call expect_call('{"t":"configure","mycall":"ABC}', '')
  call expect_call('{"t":"configure","mycall":"' // repeat('K', 200) // '"}', repeat('K', 12))

  ! MSK144's three options are booleans.
  call expect_msk144('{"t":"configure","short_messages":true,"swl":false,"train":true}', &
       [.true., .true., .true.], [.true., .false., .true.], '')
  call expect_msk144('{"t":"configure","short_messages":false,"swl":true,"train":false}', &
       [.true., .true., .true.], [.false., .true., .false.], '')
  call expect_msk144('{"t":"configure","swl":true}', [.false., .true., .false.],         &
       [.false., .true., .false.], '')
  call expect_msk144('{"t":"configure","rxfreq":1500}', [.false., .false., .false.],     &
       [.false., .false., .false.], '')
  call expect_msk144('{"t":"configure","short_messages":"yes"}', [.false., .false., .false.], &
       [.false., .false., .false.], 'short_messages')
  call expect_msk144('{"t":"configure","swl":1}', [.false., .false., .false.],           &
       [.false., .false., .false.], 'swl')
  call expect_msk144('{"t":"configure","train":null}', [.false., .false., .false.],      &
       [.false., .false., .false.], 'train')
  ! phase_eq: exactly five finite JSON numbers; otherwise no value, and the
  ! frame's one error says whether its text, its type or its range is wrong.
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,5,0,0]}', '',                &
       [0.d0, 0.d0, 5.d0, 0.d0, 0.d0])
  call expect_phase_eq('{"t":"configure","phase_eq":[ -1.5e2 ,0.25E-3,' // achar(9) //   &
       '1E+2,-0,' // achar(10) // '1.2345678901234567E-001 ]}', '',                     &
       [-150.d0, 0.25d-3, 100.d0, 0.d0, 1.2345678901234567d-1])
  call expect_phase_eq('{"t":"configure","rxfreq":1500}', '')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,5,0]}', 'range')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,5,0,0,0]}', 'range')
  call expect_phase_eq('{"t":"configure","phase_eq":[]}', 'range')
  call expect_phase_eq('{"t":"configure","phase_eq":[ ]}', 'range')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,1e999,0,0]}', 'range')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,-1e999,0,0]}', 'range')
  call expect_phase_eq('{"t":"configure","phase_eq":[1e999,0,0,0]}', 'range')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,"5",0,0]}', 'string')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,true,0,0]}', 'bool')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,null,0,0]}', 'null')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,[5],0,0]}', 'array')
  call expect_phase_eq('{"t":"configure","phase_eq":[[0,0,5,0,0]]}', 'array')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,{"x":5},0,0]}', 'object')
  call expect_phase_eq('{"t":"configure","phase_eq":5}', 'number')
  call expect_phase_eq('{"t":"configure","phase_eq":"[0,0,5,0,0]"}', 'string')
  call expect_phase_eq('{"t":"configure","phase_eq":true}', 'bool')
  call expect_phase_eq('{"t":"configure","phase_eq":null}', 'null')
  call expect_phase_eq('{"t":"configure","phase_eq":{"a":1}}', 'object')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,NaN,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,Infinity,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,+5,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,05,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,00,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,5.,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,.5,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,5e,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,5/,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,/,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,2*5,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0 5,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,5d3,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,5,0,0,]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,,5,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[1e999,0 0,0,0]}', 'text')
  call expect_phase_eq('{"t":"configure","phase_eq":[0,0,5,0,0', 'text')

  ! The GUI's "Decode after EME delay" and its live audio; both are booleans.
  call expect_flags('{"t":"configure","decode_after_eme_delay":true,"live_audio":false}', &
       .true., .true., .true., .false., '')
  call expect_flags('{"t":"configure","live_audio":true}', .false., .false., .true., .true., '')
  call expect_flags('{"t":"configure","decode_after_eme_delay":"yes"}', .false., .false., &
       .false., .false., 'decode_after_eme_delay')
  call expect_flags('{"t":"configure","live_audio":1}', .false., .false., .false., .false., &
       'live_audio')

  ! The transmitter keeps the whole call and grid; the decoder's fields keep
  ! their widths.
  call expect_station('{"t":"configure","mycall":"VE3/KJ5HST/MM","mygrid":"FN42MN12"}', &
       'VE3/KJ5HST/MM', .false., 'FN42MN12', .false.)
  call expect_call('{"t":"configure","mycall":"VE3/KJ5HST/MM","mygrid":"FN42MN12"}', &
       'VE3/KJ5HST/M', 'FN42MN')
  call expect_station('{"t":"configure","mycall":"' // repeat('K', 64) // '","mygrid":"' // &
       repeat('G', 64) // '"}', repeat('K', 64), .false., repeat('G', 64), .false.)
  call expect_station('{"t":"configure","mycall":"' // repeat('K', 65) // '","mygrid":"' // &
       repeat('G', 65) // '"}', repeat('K', 64), .true., repeat('G', 64), .true.)
  call expect_station('{"t":"configure","mycall":"' // repeat('K', 200) // '"}', &
       repeat('K', 64), .true., '', .false.)
  call expect_station('{"t":"configure","mycall":"K1\/ABC  "}', 'K1/ABC', .false., '', .false.)
  call expect_station('{"t":"configure","rxfreq":1500}', '', .false., '', .false.)
  ! White space at either end is dropped, as the GUI trims its call, before
  ! the characters are counted; the decoder's fields are as before.
  call expect_station('{"t":"configure","mycall":"  K1XYZ","mygrid":"\tEM18 \r"}', &
       'K1XYZ', .false., 'EM18', .false.)
  call expect_call('{"t":"configure","mycall":"  K1XYZ"}', '  K1XYZ')
  call expect_station('{"t":"configure","mycall":"' // repeat(' ', 70) // repeat('K', 64) // '"}', &
       repeat('K', 64), .false., '', .false.)
  call expect_station('{"t":"configure","mycall":"' // repeat('W', 64) // repeat(' ', 70) // 'Z"}', &
       repeat('W', 64), .true., '', .false.)
  call expect_station('{"t":"configure","mycall":"' // repeat(achar(195) // achar(169), 40) // '"}', &
       repeat(achar(195) // achar(169), 32), .false., '', .false.)
  ! A string that does not decode leaves the key unset and is reported.
  call expect_unreadable('{"t":"configure","mycall":"KJ5\x","mygrid":"EM18"}', .true., .false.)
  call expect_unreadable('{"t":"configure","mycall":"KJ5HST","mygrid":"EM\u12"}', .false., .true.)
  call expect_unreadable('{"t":"configure","mycall":"KJ5HST","mygrid":"EM18"}', .false., .false.)
  call expect_unreadable('{"t":"configure","rxfreq":1500,"mygrid":}', .false., .false.)

  ! Encode requests: the verbs, then each key's rule.
  call expect_action('{"t":"pack"}', CTRL_PACK)
  call expect_action('{"t":"render"}', CTRL_RENDER)
  call expect_action('{"t":"packs"}', CTRL_UNKNOWN)
  call expect_defaults('{"t":"pack","id":1,"text":"x"}')
  call expect_id('{"t":"pack","id":-2147483648,"text":"x"}', -2147483647 - 1)
  call expect_id('{"t":"pack","id":2147483647,"text":"x"}', 2147483647)
  call expect_no_id('{"t":"pack","id":2147483648,"text":"x"}')
  call expect_no_id('{"t":"pack","id":1.0,"text":"x"}')
  call expect_no_id('{"t":"pack","id":1e3,"text":"x"}')
  call expect_no_id('{"t":"pack","id":"5","text":"x"}')
  call expect_no_id('{"t":"pack","text":"x"}')
  call expect_problem('{"t":"pack","id":1,"text":"x","template":"y"}', 'template')
  call expect_problem('{"t":"pack","id":1}', 'text')
  call expect_problem('{"t":"pack","id":1,"text":5}', 'text')
  call expect_problem('{"t":"pack","id":1,"template":true}', 'template')
  call expect_template('{"t":"pack","id":1,"template":"%H %E"}', '%H %E')
  call expect_profile('{"t":"pack","id":1,"text":"x","profile":"none"}', 0)
  call expect_profile('{"t":"pack","id":1,"text":"x","profile":"field_day"}', 1)
  call expect_profile('{"t":"pack","id":1,"text":"x","profile":"rtty"}', 2)
  call expect_problem('{"t":"pack","id":1,"text":"x","profile":"unknown"}', 'profile')
  call expect_problem('{"t":"pack","id":1,"text":"x","profile":"field-day"}', 'profile')
  call expect_problem('{"t":"pack","id":1,"text":"x","profile":"rtty "}', 'profile')
  call expect_problem('{"t":"pack","id":1,"text":"x","profile":3}', 'profile')
  call expect_problem('{"t":"pack","id":1,"text":"x","final":"yes"}', 'final')
  call expect_final('{"t":"pack","id":1,"text":"x","final":false}', .false.)
  call expect_problem('{"t":"pack","id":1,"text":"x","serial":"107"}', 'serial')
  call expect_problem('{"t":"pack","id":1,"text":"x","serial":10.5}', 'serial')
  call expect_problem('{"t":"pack","id":1,"text":"x","serial":2147483648}', 'serial')
  call expect_problem('{"t":"pack","id":1,"text":"x","report":"-07"}', 'report')
  call expect_problem('{"t":"pack","id":1,"text":"x","serial":null}', 'serial')
  call expect_problem('{"t":"pack","id":1,"text":"x","his_call":7}', 'his_call')
  call expect_problem('{"t":"pack","id":1,"text":"x","exchange":false}', 'exchange')
  call expect_context('{"t":"pack","id":1,"text":"x","his_call":"W9XYZ",' // &
       '"exchange":"1D EMA","serial":-131072,"report":-7}', 'W9XYZ', '1D EMA', -131072, -7)
  call expect_render_problem('{"t":"render","id":1,"text":"x","freq":1500}', 'rate')
  call expect_render_problem('{"t":"render","id":1,"text":"x","freq":1500,"rate":44100}', 'rate')
  call expect_render_problem('{"t":"render","id":1,"text":"x","freq":1500,"rate":12000.0}', 'rate')
  call expect_render_problem('{"t":"render","id":1,"text":"x","freq":1500,"rate":"12000"}', 'rate')
  call expect_render_problem('{"t":"render","id":1,"text":"x","rate":12000,"freq":"1500"}', 'freq')
  call expect_render_problem('{"t":"render","id":1,"text":"x","rate":12000,"freq":199.99}', 'freq')
  call expect_render_problem('{"t":"render","id":1,"text":"x","rate":12000,"freq":5000.01}', 'freq')
  call expect_render_problem('{"t":"render","id":1,"text":"x","rate":12000,"freq":NaN}', 'freq')
  call expect_render_problem('{"t":"render","id":1,"text":"x","rate":12000,"freq":Infinity}', 'freq')
  call expect_render_problem('{"t":"render","id":1,"text":"x","rate":12000}', 'freq')
  call expect_render_problem('{"t":"render","id":1,"rate":12000,"freq":1500}', 'text')
  call expect_render_problem('{"t":"render","id":1,"freq":1500}', 'text')
  call expect_render_problem('{"t":"render","id":1,"text":"x","rate":12000,"freq":1500.}', 'freq')
  call expect_render_problem('{"t":"render","id":1,"text":"x","rate":12000,"freq":.5e4}', 'freq')
  call expect_render_problem('{"t":"render","id":1,"text":"x","rate":12000,"freq":3*1500}', 'freq')
  call expect_render_problem('{"t":"render","id":1,"text":"x","rate":12000,"freq":+1500}', 'freq')
  call expect_render_problem('{"t":"render","id":1,"text":"x","rate":12000,"freq":01500}', 'freq')
  call expect_render_problem('{"t":"render","id":1,"text":"x","rate":12000,"freq":1500e}', 'freq')
  call expect_render_problem('{"t":"render","id":1,"text":"x","freq":1500,"rate":+12000}', 'rate')
  call expect_render_problem('{"t":"render","id":1,"text":"x","freq":1500,"rate":012000}', 'rate')
  call expect_render_problem('{"t":"render","id":1,"text":"x","freq":1500,"rate":/}', 'rate')
  call expect_render('{"t":"render","id":1,"text":"x","rate":12000,"freq":1.5e3}', 12000, 1500.d0)
  call expect_render('{"t":"render","id":1,"text":"x","rate":12000,"freq":1234.5E0}', 12000, 1234.5d0)
  call expect_render('{"t":"render","id":1,"text":"x","rate":12000,"freq":15e+2}', 12000, 1500.d0)
  call expect_render('{"t":"render","id":1,"text":"x","rate":12000,"freq":200}', 12000, 200.d0)
  call expect_render('{"t":"render","id":1,"text":"x","rate":48000,"freq":5000}', 48000, 5000.d0)
  call expect_render('{"t":"render","id":1,"text":"x","rate":48000,"freq":1234.5}', 48000, 1234.5d0)
  call expect_accepted('{"t":"pack","id":1,"text":"x","rate":44100,"freq":"x"}')
  ! A request is one flat object; strings may hold any character.
  ! Request integers are JSON integers, which the list-directed reader alone is not.
  call expect_id('{"t":"pack","id":-0,"text":"x"}', 0)
  call expect_no_id('{"t":"pack","id":/,"text":"x"}')
  call expect_no_id('{"t":"pack","id":3*7,"text":"x"}')
  call expect_no_id('{"t":"pack","id":+5,"text":"x"}')
  call expect_no_id('{"t":"pack","id":05,"text":"x"}')
  call expect_no_id('{"t":"pack","id":1e2,"text":"x"}')
  call expect_problem('{"t":"pack","id":1,"text":"x","serial":/}', 'serial')
  call expect_problem('{"t":"pack","id":1,"text":"x","serial":1*}', 'serial')
  call expect_problem('{"t":"pack","id":1,"text":"x","serial":+5}', 'serial')
  call expect_problem('{"t":"pack","id":1,"text":"x","serial":05}', 'serial')
  call expect_problem('{"t":"pack","id":1,"text":"x","serial":2*5}', 'serial')
  call expect_problem('{"t":"pack","id":1,"text":"x","report":/}', 'report')
  call expect_problem('{"t":"pack","id":1,"text":"x","report":-05}', 'report')
  call expect_context('{"t":"pack","id":1,"text":"x","his_call":"W","exchange":"E","serial":0,"report":-0 }', 'W', 'E', 0, 0)
  call expect_problem('{"t":"pack","id":1,"text":"x","context":{"his_call":"W9XYZ"}}', '')
  call expect_problem('{"t":"pack","id":1,"text":"x","serial":[1]}', '')
  call expect_problem('{"t":"pack","id":1,"text":"x"}{"t":"pack"}', '')
  call expect_text('{"t":"pack","id":1,"text":"{[x]}"}', '{[x]}')
  call expect_text('{"t":"pack","id":1,"text":"a\"{["}', 'a"{[')
  call expect_text('{"t":"pack","id":1,"text":"a\\","his_call":"{"}', 'a\')
  call expect_text('{"t":"pack","id":1,"text":"A\/B\\C\"D\b\f\n\r\tE\u004B\u00e9\u20AC\ud83d\ude00\uD83D\uDE00\u0000F"}', &
       'A/B\C"D' // achar(8) // achar(12) // achar(10) // achar(13) // achar(9) // 'EK' // &
       char(195) // char(169) // char(226) // char(130) // char(172) // &
       repeat(char(240) // char(159) // char(152) // char(128), 2) // achar(0) // 'F')
  call expect_problem('{"t":"pack","id":1,"text":"A\x"}', 'text')
  call expect_problem('{"t":"pack","id":1,"text":"x","his_call":"A\x"}', 'his_call')
  call expect_problem('{"t":"pack","id":1,"text":"\ud83d"}', 'text')
  call expect_problem('{"t":"pack","id":1,"text":"x","his_call":"\ud83d"}', 'his_call')
  call expect_problem('{"t":"pack","id":1,"text":"A\u12G4"}', 'text')
  call expect_problem('{"t":"pack","id":1,"text":"x","his_call":"A\u12G4"}', 'his_call')
  call expect_problem('{"t":"pack","id":1,"text":"\ude00"}', 'text')
  call expect_problem('{"t":"pack","id":1,"text":"x","his_call":"\ude00"}', 'his_call')
  call expect_problem('{"t":"pack","id":1,"text":"x","exchange":"\udc00"}', 'exchange')
  call expect_profile('{"t":"pack","id":1,"text":"x","profile":"rt\u0074y"}', 2)
  call expect_problem('{"t":"pack","id":1,"text":"abc}', 'text')
  long_text = repeat('0123456789', 10000)
  call expect_text('{"t":"pack","id":1,"text":"' // long_text // '"}', long_text)

  if (nfail .eq. 0) then
     print '(a)', 'ALL STREAMING CONTROL CHECKS PASSED'
  else
     print '(a,i0,a)', 'STREAMING CONTROL: ', nfail, ' CHECK(S) FAILED'
     call exit(1)
  end if

contains

  subroutine expect_action(frame, expected)
    character(len=*), intent(in) :: frame
    integer,          intent(in) :: expected
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action

    call parse_control_frame(frame, action, cfg, terr)
    if (action .eq. expected) then
       print '(a,a)', '  PASS  ', frame
    else
       print '(a,a,a,i0)', '  FAIL  ', frame, ' -> action ', action
       nfail = nfail + 1
    end if
  end subroutine expect_action

  ! set: the frame selects mode; otherwise its mode is reported invalid.
  subroutine expect_mode(frame, set, mode)
    character(len=*), intent(in) :: frame
    logical,          intent(in) :: set
    integer,          intent(in) :: mode
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action

    call parse_control_frame(frame, action, cfg, terr)
    if (action .eq. CTRL_CONFIGURE .and. .not. terr%present .and.            &
         (cfg%mode_set .eqv. set) .and. (cfg%mode_invalid .neqv. set) .and.  &
         (.not. set .or. cfg%mode .eq. mode)) then
       print '(a,a)', '  PASS  ', frame
    else
       print '(a,a,a,l1,a,i0)', '  FAIL  ', frame, ' -> mode_set ', cfg%mode_set, &
            ' mode ', cfg%mode
       nfail = nfail + 1
    end if
  end subroutine expect_mode

  ! The two flags as parsed, or the key a type error names.
  subroutine expect_flags(frame, eme_set, eme, live_set, live, bad_key)
    character(len=*), intent(in) :: frame, bad_key
    logical,          intent(in) :: eme_set, eme, live_set, live
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    logical :: good

    call parse_control_frame(frame, action, cfg, terr)
    if (len(bad_key) .gt. 0) then
       good = terr%present .and. trim(terr%key) .eq. bad_key
    else
       good = action .eq. CTRL_CONFIGURE .and. .not. terr%present .and.      &
            (cfg%decode_after_eme_delay_set .eqv. eme_set) .and.             &
            (cfg%live_audio_set .eqv. live_set) .and.                         &
            (.not. eme_set .or. (cfg%decode_after_eme_delay .eqv. eme)) .and. &
            (.not. live_set .or. (cfg%live_audio .eqv. live))
    end if
    if (good) then
       print '(a,a)', '  PASS  ', frame
    else
       print '(a,a)', '  FAIL  ', frame
       nfail = nfail + 1
    end if
  end subroutine expect_flags

  ! short_messages, swl and train as parsed (set, then value), or the key a
  ! type error names.
  subroutine expect_msk144(frame, set, val, bad_key)
    character(len=*), intent(in) :: frame, bad_key
    logical,          intent(in) :: set(3), val(3)
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    logical :: good

    call parse_control_frame(frame, action, cfg, terr)
    if (len(bad_key) .gt. 0) then
       good = terr%present .and. trim(terr%key) .eq. bad_key .and.            &
            .not. (cfg%short_messages_set .or. cfg%swl_set .or. cfg%train_set)
    else
       good = action .eq. CTRL_CONFIGURE .and. .not. terr%present .and.      &
            all([cfg%short_messages_set, cfg%swl_set, cfg%train_set] .eqv. set) .and. &
            all(([cfg%short_messages, cfg%swl, cfg%train] .eqv. val) .or. .not. set)
    end if
    if (good) then
       print '(a,a)', '  PASS  ', frame
    else
       print '(a,a)', '  FAIL  ', frame
       nfail = nfail + 1
    end if
  end subroutine expect_msk144

  ! phase_eq as parsed (values; absent: unset), or refused for its 'text' or
  ! 'range', or with outcome the category its type error names.
  subroutine expect_phase_eq(frame, outcome, values)
    character(len=*),  intent(in)           :: frame, outcome
    real(8),           intent(in), optional :: values(5)
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    logical :: good, typed

    call parse_control_frame(frame, action, cfg, terr)
    typed = terr%present .and. trim(terr%key) .eq. 'phase_eq'
    good = action .eq. CTRL_CONFIGURE
    select case (outcome)
    case ('')
       good = good .and. .not. (terr%present .or. cfg%phase_eq_malformed .or.  &
            cfg%phase_eq_out_of_range) .and. (cfg%phase_eq_set .eqv. present(values))
       if (present(values)) good = good .and. all(cfg%phase_eq .eq. values)
    case ('text')
       good = good .and. cfg%phase_eq_malformed .and. .not. (terr%present .or.   &
            cfg%phase_eq_out_of_range .or. cfg%phase_eq_set)
    case ('range')
       good = good .and. cfg%phase_eq_out_of_range .and. .not. (terr%present .or. &
            cfg%phase_eq_malformed .or. cfg%phase_eq_set)
    case default
       good = good .and. typed .and. trim(terr%expected) .eq. 'array of numbers' .and. &
            trim(terr%got) .eq. outcome .and. .not. (cfg%phase_eq_malformed .or.   &
            cfg%phase_eq_out_of_range .or. cfg%phase_eq_set)
    end select
    if (good) then
       print '(a,a)', '  PASS  ', frame
    else
       print '(a,a,a,l1,a,l1,a,l1,a,a,a,5(1x,es24.16))', '  FAIL  ', frame, ' -> set ', &
            cfg%phase_eq_set, ' text ', cfg%phase_eq_malformed, ' range ',          &
            cfg%phase_eq_out_of_range, ' got ', trim(terr%got), ' values', cfg%phase_eq
       nfail = nfail + 1
    end if
  end subroutine expect_phase_eq

  subroutine expect_tx_mode_unset(frame)
    character(len=*), intent(in) :: frame
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action

    call parse_control_frame(frame, action, cfg, terr)
    if (action .eq. CTRL_CONFIGURE .and. .not. terr%present .and. .not. cfg%tx_mode_set) then
       print '(a,a)', '  PASS  ', frame
    else
       print '(a,a,a,i0)', '  FAIL  ', frame, ' -> tx_mode ', cfg%tx_mode
       nfail = nfail + 1
    end if
  end subroutine expect_tx_mode_unset

  ! The frame sets mycall to these bytes ('' expects it unset), and mygrid
  ! to grid when one is given; no type error either way.
  subroutine expect_call(frame, mycall, grid)
    character(len=*), intent(in)           :: frame, mycall
    character(len=*), intent(in), optional :: grid
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action, k
    logical :: ok

    call parse_control_frame(frame, action, cfg, terr)
    ok = action .eq. CTRL_CONFIGURE .and. .not. terr%present .and.            &
         (cfg%mycall_set .eqv. len(mycall) .gt. 0) .and. cfg%mycall .eq. mycall
    if (present(grid)) ok = ok .and. cfg%mygrid_set .and. cfg%mygrid .eq. grid
    if (ok) then
       print '(a,a)', '  PASS  ', frame
    else
       print '(a,a,a,l1,a,12(1x,i0))', '  FAIL  ', frame, ' -> mycall_set ',   &
            cfg%mycall_set, ' bytes', [(iachar(cfg%mycall(k:k)), k = 1, 12)]
       nfail = nfail + 1
    end if
  end subroutine expect_call

  subroutine report(frame, ok, why)
    character(len=*), intent(in) :: frame, why
    logical,          intent(in) :: ok
    if (ok) then
       print '(a,a)', '  PASS  ', frame(1:min(len(frame), 100))
    else
       print '(a,a,a,a)', '  FAIL  ', frame(1:min(len(frame), 100)), ' -> ', why
       nfail = nfail + 1
    end if
  end subroutine report

  ! The transmitter's copies of mycall and mygrid ('' expects the key unset).
  subroutine expect_station(frame, call_tx, call_long, grid_tx, grid_long)
    character(len=*), intent(in) :: frame, call_tx, grid_tx
    logical,          intent(in) :: call_long, grid_long
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action

    call parse_control_frame(frame, action, cfg, terr)
    call report(frame, action .eq. CTRL_CONFIGURE .and. .not. terr%present .and.   &
         (cfg%mycall_set .eqv. len(call_tx) .gt. 0) .and. cfg%mycall_tx .eq. call_tx .and. &
         (cfg%mycall_tx_long .eqv. call_long) .and.                              &
         (cfg%mygrid_set .eqv. len(grid_tx) .gt. 0) .and. cfg%mygrid_tx .eq. grid_tx .and. &
         (cfg%mygrid_tx_long .eqv. grid_long), 'transmitter copies')
  end subroutine expect_station

  subroutine expect_unreadable(frame, call_unreadable, grid_unreadable)
    character(len=*), intent(in) :: frame
    logical,          intent(in) :: call_unreadable, grid_unreadable
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action

    call parse_control_frame(frame, action, cfg, terr)
    call report(frame, action .eq. CTRL_CONFIGURE .and. .not. terr%present .and.   &
         (cfg%mycall_unreadable .eqv. call_unreadable) .and.                     &
         (cfg%mygrid_unreadable .eqv. grid_unreadable) .and.                     &
         .not. (call_unreadable .and. cfg%mycall_set) .and.                      &
         .not. (grid_unreadable .and. cfg%mygrid_set), 'unreadable flags')
  end subroutine expect_unreadable

  ! A pack request that reads with no problem.
  logical function accepted(frame, req)
    character(len=*),     intent(in)  :: frame
    type(encode_request), intent(out) :: req
    call parse_encode_request(frame, req)
    accepted = req%id_ok .and. len(req%problem) .eq. 0
  end function accepted

  subroutine expect_accepted(frame)
    character(len=*), intent(in) :: frame
    type(encode_request) :: req
    call report(frame, accepted(frame, req), req%problem)
  end subroutine expect_accepted

  ! Every key absent: final, profile none, empty context strings, and no
  ! serial or report.
  subroutine expect_defaults(frame)
    character(len=*), intent(in) :: frame
    type(encode_request) :: req
    call report(frame, accepted(frame, req) .and. req%id .eq. 1 .and.  &
         .not. req%is_template .and. req%text .eq. 'x' .and. len(req%text) .eq. 1 .and. &
         req%final .and. req%profile .eq. 0 .and. len(req%his_call) .eq. 0 .and.  &
         len(req%exchange) .eq. 0 .and. .not. req%serial_given .and.           &
         .not. req%report_given, 'not the defaults')
  end subroutine expect_defaults

  subroutine expect_id(frame, id)
    character(len=*), intent(in) :: frame
    integer,          intent(in) :: id
    type(encode_request) :: req
    call report(frame, accepted(frame, req) .and. req%id .eq. id, 'id')
  end subroutine expect_id

  subroutine expect_no_id(frame)
    character(len=*), intent(in) :: frame
    type(encode_request) :: req
    call parse_encode_request(frame, req)
    call report(frame, .not. req%id_ok, 'id accepted')
  end subroutine expect_no_id

  ! The pack request has an id and a problem with key ('' for none).
  subroutine expect_problem(frame, key)
    character(len=*), intent(in) :: frame, key
    type(encode_request) :: req
    call parse_encode_request(frame, req)
    call report_problem(frame, req, key)
  end subroutine expect_problem

  subroutine report_problem(frame, req, key)
    character(len=*),     intent(in) :: frame, key
    type(encode_request), intent(in) :: req
    call report(frame, req%id_ok .and. len(req%problem) .gt. 0 .and.         &
         req%problem_key .eq. key .and. len(req%problem_key) .eq. len(key),     &
         'key ' // req%problem_key // ': ' // req%problem)
  end subroutine report_problem

  subroutine expect_template(frame, template)
    character(len=*), intent(in) :: frame, template
    type(encode_request) :: req
    call report(frame, accepted(frame, req) .and. req%is_template .and. &
         req%text .eq. template .and. len(req%text) .eq. len(template), 'template')
  end subroutine expect_template

  ! The text's exact bytes.
  subroutine expect_text(frame, text)
    character(len=*), intent(in) :: frame, text
    type(encode_request) :: req
    call report(frame, accepted(frame, req) .and. .not. req%is_template .and. &
         len(req%text) .eq. len(text) .and. req%text .eq. text, 'text bytes')
  end subroutine expect_text

  subroutine expect_profile(frame, profile)
    character(len=*), intent(in) :: frame
    integer,          intent(in) :: profile
    type(encode_request) :: req
    call report(frame, accepted(frame, req) .and. req%profile .eq. profile, 'profile')
  end subroutine expect_profile

  subroutine expect_final(frame, final)
    character(len=*), intent(in) :: frame
    logical,          intent(in) :: final
    type(encode_request) :: req
    call report(frame, accepted(frame, req) .and. (req%final .eqv. final), 'final')
  end subroutine expect_final

  subroutine expect_context(frame, his_call, exchange, serial, report_value)
    character(len=*), intent(in) :: frame, his_call, exchange
    integer,          intent(in) :: serial, report_value
    type(encode_request) :: req
    call report(frame, accepted(frame, req) .and. req%his_call .eq. his_call .and. &
         len(req%his_call) .eq. len(his_call) .and. req%exchange .eq. exchange .and. &
         len(req%exchange) .eq. len(exchange) .and. req%serial_given .and.        &
         req%serial .eq. serial .and. req%report_given .and. req%report .eq. report_value, &
         'context')
  end subroutine expect_context

  subroutine expect_render_problem(frame, key)
    character(len=*), intent(in) :: frame, key
    type(encode_request) :: req
    call parse_render_request(frame, req)
    call report_problem(frame, req, key)
  end subroutine expect_render_problem

  subroutine expect_render(frame, rate, freq)
    character(len=*), intent(in) :: frame
    integer,          intent(in) :: rate
    real(8),          intent(in) :: freq
    type(encode_request) :: req
    call parse_render_request(frame, req)
    call report(frame, req%id_ok .and. len(req%problem) .eq. 0 .and. req%rate .eq. rate .and. &
         req%freq .eq. freq, 'render')
  end subroutine expect_render

end program test_streaming_control
