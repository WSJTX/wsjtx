program test_map65_decode_requests
  use iso_c_binding
  use npar_ptrs_mod
  implicit none

  integer(c_int64_t) :: early_id, final_id, manual_id
  character(kind=c_char) :: dx(12)

  call set_nhsym(280)
  call set_nagain(0)
  call set_ndepth(1)
  call set_fcenter(144.125_c_double)
  dx = transfer('K1ABC       ', dx)
  call set_hiscall(dx)
  early_id = publish_decode_request()
  if (.not. claim_decode_request()) error stop 'early request missing'
  if (active_request_id /= early_id .or. nhsym /= 280) error stop 'early identity changed'
  if (claim_decode_request()) error stop 'early request repeated'

  ! Publish the final pass while the early pass is still using its parameters.
  call set_decoder_ready(0)
  call set_nhsym(302)
  call set_ndepth(3)
  call set_fcenter(144.135_c_double)
  dx = transfer('K2DEF       ', dx)
  call set_hiscall(dx)
  final_id = publish_decode_request()
  if (nhsym /= 280 .or. ndepth /= 1 .or. hiscall /= 'K1ABC') error stop 'active parameters changed'
  if (abs(fcenter - 144.125_c_double) > 1.e-8_c_double) error stop 'active center frequency changed'
  if (is_current_decode_request(early_id)) error stop 'stale early completion accepted'
  newdat = 0
  if (.not. claim_decode_request()) error stop 'pending final lost'
  if (active_request_id /= final_id .or. nhsym /= 302 .or. newdat /= 1) error stop 'final identity changed'
  if (ndepth /= 3 .or. hiscall /= 'K2DEF') error stop 'final parameters changed'
  if (.not. is_current_decode_request(final_id)) error stop 'current final completion rejected'
  if (claim_decode_request()) error stop 'final request repeated'

  ! Both automatic triggers can arrive before the decoder claims either.
  call set_nhsym(280)
  early_id = publish_decode_request()
  call set_nhsym(302)
  final_id = publish_decode_request()
  call set_decoder_ready(0)
  if (claim_decode_request()) error stop 'unready request claimed'
  call set_decoder_ready(1)
  if (.not. claim_decode_request()) error stop 'queued early missing'
  if (active_request_id /= early_id .or. nhsym /= 280) error stop 'queued early out of order'
  if (.not. claim_decode_request()) error stop 'queued final missing'
  if (active_request_id /= final_id .or. nhsym /= 302) error stop 'queued final out of order'
  if (claim_decode_request()) error stop 'queue did not drain'

  ! Completing a manual request cannot clear a newer automatic request.
  call set_manual_decode_flag(1)
  call set_nagain(1)
  manual_id = publish_decode_request()
  if (get_manual_decode_flag() /= 0) error stop 'manual flag not consumed'
  if (.not. claim_decode_request()) error stop 'manual request missing'
  if (manualDecodeFlag /= 1 .or. nagain /= 1) error stop 'manual parameters changed'
  call set_nagain(0)
  final_id = publish_decode_request()
  if (manualDecodeFlag /= 1 .or. nagain /= 1) error stop 'pending update changed active manual'
  manualDecodeFlag = 0
  newdat = 0
  if (is_current_decode_request(manual_id)) error stop 'stale manual completion accepted'
  if (.not. claim_decode_request()) error stop 'final lost after manual completion'
  if (active_request_id /= final_id .or. manualDecodeFlag /= 0 .or. nagain /= 0) error stop 'final inherited manual parameters'
  if (claim_decode_request()) error stop 'manual sequence did not drain'

  ! A final request waiting beyond rollover must not decode the next interval.
  call set_ndiskdat(0)
  call set_nagain(0)
  final_id = publish_decode_request()
  call advance_live_input_generation()
  if (.not. claim_decode_request()) error stop 'expired final missing'
  if (active_request_id /= final_id .or. .not. active_request_expired) error stop 'old final did not expire'
  if (.not. is_current_decode_request(final_id)) error stop 'expired current request cannot clear busy'
  early_id = publish_decode_request()
  if (is_current_decode_request(final_id)) error stop 'old skipped completion accepted'
  if (.not. claim_decode_request()) error stop 'current interval request missing'
  if (active_request_id /= early_id .or. active_request_expired) error stop 'current interval request expired'
  if (active_input_generation /= live_input_generation) error stop 'claimed input generation changed'

  call set_nagain(1)
  manual_id = publish_decode_request()
  call advance_live_input_generation()
  if (.not. claim_decode_request()) error stop 'manual repeat missing after rollover'
  if (active_request_id /= manual_id .or. active_request_expired) error stop 'manual repeat expired on rollover'
  call set_nagain(0)
  call set_manual_decode_flag(1)
  manual_id = publish_decode_request()
  call advance_live_input_generation()
  if (.not. claim_decode_request()) error stop 'freeze request missing after rollover'
  if (active_request_id /= manual_id .or. active_request_expired) error stop 'freeze request expired on rollover'
  call set_ndiskdat(1)
  final_id = publish_decode_request()
  call advance_live_input_generation()
  if (.not. claim_decode_request()) error stop 'disk request missing after rollover'
  if (active_request_id /= final_id .or. active_request_expired) error stop 'disk request expired on rollover'
  call set_ndiskdat(0)

  early_id = publish_decode_request()
  final_id = publish_decode_request()
  call cancel_pending_decode_requests()
  if (claim_decode_request()) error stop 'canceled request claimed'
  if (is_current_decode_request(final_id)) error stop 'canceled completion accepted'
  final_id = publish_decode_request()
  if (.not. claim_decode_request()) error stop 'request missing after cancel'
  if (active_request_id /= final_id) error stop 'wrong request after cancel'
  early_id = publish_decode_request()
  call set_stop_m65(1)
  if (get_stop_m65() /= 1 .or. claim_decode_request()) error stop 'shutdown did not discard requests'
end program
