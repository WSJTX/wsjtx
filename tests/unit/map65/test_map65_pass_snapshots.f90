program test_map65_pass_snapshots
  ! Each automatic decode pass must decode data frozen at the moment it was
  ! triggered. Simulates the new minute resetting the live buffers while the
  ! 302-symbol pass is still queued behind the running 280-symbol pass.
  use iso_c_binding, only: c_float
  use datcom_ptrs_mod, only: dd, ss, savg, snap, snapshot_buffers
  use npar_ptrs_mod, only: snap_latest_idx, set_nhsym, set_nagain, set_ndiskdat, &
       publish_decode_request, claim_decode_request, active_request_expired, &
       active_snap_idx, snapshot_release, advance_live_input_generation
  implicit none

  integer, parameter :: nsmax = 4000, ncol = 12, nfft = 16
  integer(8) :: rid

  allocate(dd(4,nsmax), ss(4,ncol,nfft), savg(4,nfft))
  allocate(snap(1)%dd(4,nsmax), snap(1)%ss(4,ncol,nfft), snap(1)%savg(4,nfft))
  allocate(snap(2)%dd(4,nsmax), snap(2)%ss(4,ncol,nfft), snap(2)%savg(4,nfft))
  snap(1)%dd = -1.0; snap(1)%ss = -1.0; snap(1)%savg = -1.0
  snap(2)%dd = -1.0; snap(2)%ss = -1.0; snap(2)%savg = -1.0

  ! Early pass (nhsym 280 stand-in): live buffers hold cycle A up to sample 3000
  dd = 0.0; dd(:,1:3000) = 1.0
  ss = 0.0; ss(:,1:10,:) = 1.0
  savg = 10.0
  call snapshot_buffers(1, 3000, 10)
  call require(snap_latest_idx == 1, 'latest snapshot is the early one')

  ! Later: live buffers keep filling, then the new minute resets them
  dd(:,3001:3800) = 2.0
  ss(:,11:12,:) = 2.0
  savg = 20.0
  call snapshot_buffers(2, 3800, 12)
  call require(snap_latest_idx == 2, 'latest snapshot is the final one')

  dd = 7.0; ss = 7.0; savg = 0.0          !Next cycle overwrites / resets live

  call require(all(snap(1)%dd(:,1:3000) == 1.0), 'early dd frozen')
  call require(all(snap(1)%dd(:,3513:) == 0.0), 'early dd past lookahead is zero')
  call require(all(snap(1)%ss(:,1:10,:) == 1.0), 'early ss frozen')
  call require(all(snap(1)%ss(:,11:,:) == 0.0), 'early ss past nhsym is zero')
  call require(all(snap(1)%savg == 10.0), 'early savg frozen')

  call require(all(snap(2)%dd(:,1:3000) == 1.0), 'final dd has early part')
  call require(all(snap(2)%dd(:,3001:3800) == 2.0), 'final dd has later part')
  call require(all(snap(2)%ss(:,11:12,:) == 2.0), 'final ss frozen')
  call require(all(snap(2)%savg == 20.0), 'final savg frozen')

  ! ---- request expiry follows the snapshot, not the minute boundary ----
  call set_nagain(0); call set_ndiskdat(0)

  ! 1. A final-pass request claimed after the minute rolled over still runs
  !    (its snapshot is intact), and holds its slot while running.
  dd = 3.0; ss = 3.0; savg = 3.0
  call set_nhsym(302)
  call snapshot_buffers(2, 3800, 12)
  rid = publish_decode_request()
  call advance_live_input_generation()          !new minute begins
  call require(claim_decode_request(), 'request claimed')
  call require(.not. active_request_expired, 'late request with intact snapshot runs')
  call require(active_snap_idx == 2, 'late request reads its own slot')

  ! 2. While that pass is running, the next cycle's fill must not overwrite
  !    the slot; the request published for it is then dropped, not mis-decoded.
  dd = 4.0
  call snapshot_buffers(2, 3800, 12)
  call require(all(snap(2)%dd(:,1:3800) == 3.0), 'busy slot not overwritten')
  rid = publish_decode_request()
  call snapshot_release()
  call require(claim_decode_request(), 'second request claimed')
  call require(active_request_expired, 'request whose fill was skipped is dropped')

  ! 3. A queued request whose slot is refilled before it is claimed is dropped.
  dd = 5.0
  call snapshot_buffers(2, 3800, 12)
  rid = publish_decode_request()
  dd = 6.0
  call snapshot_buffers(2, 3800, 12)
  call require(claim_decode_request(), 'third request claimed')
  call require(active_request_expired, 'overwritten snapshot request is dropped')

  ! 4. More than one generation old (monitoring stopped etc.) is dropped.
  call snapshot_buffers(2, 3800, 12)
  rid = publish_decode_request()
  call advance_live_input_generation()
  call advance_live_input_generation()
  call require(claim_decode_request(), 'fourth request claimed')
  call require(active_request_expired, 'request two generations old is dropped')
  print *, 'test_map65_pass_snapshots: PASS'

contains

  subroutine require(ok, what)
    logical, intent(in) :: ok
    character(*), intent(in) :: what
    if (.not. ok) then
       print *, 'FAIL: ', what
       error stop 1
    end if
  end subroutine require
end program test_map65_pass_snapshots
