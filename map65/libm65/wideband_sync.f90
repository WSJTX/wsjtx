module wideband_sync

   use iso_fortran_env, only: real32

   type candidate
      real :: snr          !Relative S/N of sync detection
      real :: f            !Freq of sync tone, 0 to 96000 Hz
      real :: xdt          !DT of matching sync pattern, -1.0 to +4.6 s
      real :: pol          !Polarization angle, degrees
      integer :: ipol      !Polarization angle, 1 to 4 ==> 0, 45, 90, 135 deg
      integer :: iflip     !Sync type: JT65 = +/- 1, Q65 = 0
      integer :: indx
   end type candidate
   type sync_dat
      real :: ccfmax
      real :: xdt
      real :: pol
      integer :: ipol
      integer :: iflip
      logical :: birdie
   end type sync_dat

   integer, parameter :: MAX_CANDIDATES = 50
   real(real32), parameter :: SNR1_THRESHOLD = 4.5
   type(sync_dat), allocatable :: sync(:)  !NFFT
   integer nkhz_center

contains

   subroutine get_candidates(ss, savg, xpol, jz, nfa, nfb, nts_jt65, nts_q65, cand, ncand)
      use iso_c_binding
      use debug_log, only: dbg, itoa, rtoa
      use indexx_mod, only: indexx
      use iso_fortran_env, only: real64
      use npar_ptrs_mod, only: nrate_active, nfft_active
      
      implicit none

      !==== Dummy arguments =====================================================
      real,    intent(in)    :: ss(4,322,nfft_active)
      real,    intent(in)    :: savg(4,nfft_active)
      logical, intent(in)    :: xpol
      integer, intent(in)    :: jz, nfa, nfb
      integer, intent(in)    :: nts_jt65, nts_q65
      type(candidate), intent(out) :: cand(MAX_CANDIDATES)
      integer, intent(out)   :: ncand


! Search symbol spectra ss() over frequency range nfa to nfb (in kHz) for
! JT65 and Q65 sync patterns. The nts_* variables are the submode tone
! spacings: 1 2 4 8 16 for A B C D E.  Birdies are detected and
! excised.  Candidates are returned in the structure array cand().

      integer, parameter :: MAX_PEAKS = 100
      real pavg(-20:20)
      real base, bw, diffhz, df3, flip, flip_top, pmax, snr1, snr_top, tstep
      real(real64) f0
      integer i, ia, ib, ipol, iz, j
      integer j1, j2, k, n, n_top, jsum, m
      integer, allocatable :: indx(:)
      logical skip

      call wb_sync(ss, savg, xpol, jz, nfa, nfb)          !Output to sync() array

      tstep = 2048.0/11025.0        !0.185760 s: 0.5*tsym_jt65, 0.3096*tsym_q65
      df3 = real(nrate_active)/real(nfft_active)
      ia = nint(1000*nfa/df3) + 1
      ib = nint(1000*nfb/df3) + 1
      if (ia .lt. 1) ia = 1
      if (ib .gt. nfft_active - 1) ib = nfft_active - 1
      iz = ib - ia + 1
                  
      allocate (indx(iz))

      ! w3sz this if block should not be necessary but it is
      if (iz <= 0) then
!         print *, 'GET_CAND: iz <= 0, ia=', ia, ' ib=', ib
         flush (6)
         return
      endif

      call indexx(sync(ia:ib)%ccfmax, iz, indx)   !Sort by relative snr

      n_top = indx(iz) + ia - 1
      snr_top = sync(n_top)%ccfmax
      flip_top = sync(n_top)%iflip
            
      k = 0
      do i = 1, MAX_PEAKS
         if ((iz + 1 - i) .lt. 1) cycle !w3sz debug
         n = indx(iz + 1 - i) + ia - 1
         f0 = 0.001*(n - 1)*df3
         snr1 = sync(n)%ccfmax
         if (snr1 .lt. SNR1_THRESHOLD) exit
         flip = sync(n)%iflip
         if (flip .ne. 0.0 .and. nts_jt65 .eq. 0) cycle
         if (flip .eq. 0.0 .and. nts_q65 .eq. 0) cycle
         if (sync(n)%birdie) cycle

! Test for signal outside of TxT range and set bw for this signal type
         j1 = int((sync(n)%xdt + 1.0)/tstep - 1.0)
         j2 = int((sync(n)%xdt + 52.0)/tstep + 1.0)
         if (flip .ne. 0) j2 = int((sync(n)%xdt + 47.811)/tstep + 1.0)
         ipol = sync(n)%ipol
         pavg = 0.
         do j = 1, j1
            pavg = pavg + ss(ipol, j, n - 20:n + 20)
         enddo
         do j = j2, jz
            pavg = pavg + ss(ipol, j, n - 20:n + 20)
         enddo
         jsum = j1 + (jz - j2 + 1)
         pmax = maxval(pavg(-2:2))              !### Why not just pavg(0) ?
         base = (sum(pavg) - pmax)/jsum
         pmax = pmax/base
         if (pmax .gt. 5.0) cycle
         skip = .false.
         do m = 1, k                              !Skip false syncs within signal bw
            if (cand(m)%iflip .ne. nint(flip)) cycle   !only dedupe within the same type
            diffhz = 1000.0*(f0 - cand(m)%f)
            bw = nts_q65*110.0
            if (cand(m)%iflip .ne. 0) bw = nts_jt65*178.0
            if (diffhz .gt. -0.03*bw .and. diffhz .lt. 1.03*bw) skip = .true.
         enddo
         if (skip) cycle
         k = k + 1
         cand(k)%snr = snr1
         cand(k)%f = f0
         cand(k)%xdt = sync(n)%xdt
         cand(k)%pol = sync(n)%pol
         cand(k)%ipol = sync(n)%ipol
         cand(k)%iflip = nint(flip)
         cand(k)%indx = n            
         
!     write(50,3050) i,k,m,f0+32.0,diffhz,bw,snr1,db(snr1)
!3050 format(3i5,f8.3,2f8.0,2f8.2)
         if (k .ge. MAX_CANDIDATES) exit
      enddo
      ncand = k
      return
   end subroutine get_candidates

   subroutine wb_sync(ss, savg, xpol, jz, nfa, nfb)
      use iso_c_binding
      use debug_log
      use indexx_mod
      use txpol_mod
      use trimlist_mod
      use pctile_mod
      use polfit_mod
      use npar_ptrs_mod, only: nrate_active, nfft_active
      implicit none

      !==== Dummy arguments =====================================================
      real,    intent(in)    :: ss(4,322,nfft_active)
      real,    intent(in)    :: savg(4,nfft_active)
      logical, intent(in)    :: xpol
      integer, intent(in)    :: jz, nfa, nfb

      integer, parameter :: LAGMAX = 30
      integer, parameter :: Q65_SYNC_ROWS = 3*22, JT65_SYNC_ROWS = 2*63
      integer, parameter :: Q65_MIN_ROWS = Q65_SYNC_ROWS - 3
      integer, parameter :: JT65_MIN_ROWS = JT65_SYNC_ROWS - 2
      real(c_float) :: savg_med(4)
      real ccf4(4), ccf4best(4), a(3)
      real base, ccf, ccfmax, df3, fac, flip, poldeg, row_scale, tstep
      integer i, ia, ib, ipolbest, j, k, lag, lagbest, nrows
      integer npol, ipol
      logical first
      integer isync(22)
      integer jsync0(63), jsync1(63)
      integer q65_available(22,0:LAGMAX), q65_row_count(0:LAGMAX)
      integer jt65_0_available(63,0:LAGMAX), jt65_0_row_count(0:LAGMAX)
      integer jt65_1_available(63,0:LAGMAX), jt65_1_row_count(0:LAGMAX)
      integer ip(1)
      
        ! --- JT65 wideband debugging probe ---
        ! real :: freq_hz

! Q65 sync symbols
      data isync/1, 9, 12, 13, 15, 22, 23, 26, 27, 33, 35, 38, 46, 50, 55, 60, 62, 66, 69, 74, 76, 85/
      data jsync0/ &
         1, 4, 5, 9, 10, 11, 12, 13, 14, 16, 18, 22, 24, 25, 28, 32, &
         33, 34, 37, 38, 39, 40, 42, 43, 45, 46, 47, 48, 52, 53, 55, 57, &
         59, 60, 63, 64, 66, 68, 70, 73, 80, 81, 89, 90, 92, 95, 97, 98, &
         100, 102, 104, 107, 108, 111, 114, 119, 120, 121, 122, 123, 124, 125, 126/
      data jsync1/ &
         2, 3, 6, 7, 8, 15, 17, 19, 20, 21, 23, 26, 27, 29, 30, 31, &
         35, 36, 41, 44, 49, 50, 51, 54, 56, 58, 61, 62, 65, 67, 69, 71, &
         72, 74, 75, 76, 77, 78, 79, 82, 83, 84, 85, 86, 87, 88, 91, 93, &
         94, 96, 99, 101, 103, 105, 106, 109, 110, 112, 113, 115, 116, 117, 118/
      data first/.true./
      save first, isync, jsync0, jsync1

      tstep = 2048.0/11025.0        !0.185760 s: 0.5*tsym_jt65, 0.3096*tsym_q65
      if (first) then
         fac = 0.6/tstep
         do i = 1, 22                                !Expand the Q65 sync stride
            isync(i) = nint((isync(i) - 1)*fac) + 1
         enddo
         do i = 1, 63
            jsync0(i) = 2*(jsync0(i) - 1) + 1
            jsync1(i) = 2*(jsync1(i) - 1) + 1
         enddo
         first = .false.
      endif

      df3 = real(nrate_active)/real(nfft_active)
      ia = nint(1000*nfa/df3) + 1          !Flat frequency range for WSE converters
      ib = nint(1000*nfb/df3) + 1
      if (ia .lt. 1) ia = 1
      if (ib .gt. nfft_active - 1) ib = nfft_active - 1
      npol = 1
      if (xpol) npol = 4

      q65_row_count = 0
      jt65_0_row_count = 0
      jt65_1_row_count = 0
      do lag = 0, LAGMAX
         do j = 1, 22
            k = isync(j) + lag
            q65_available(j,lag) = min(3,max(0,jz-k+1))
            q65_row_count(lag) = q65_row_count(lag) + q65_available(j,lag)
         enddo
         do j = 1, 63
            k = jsync0(j) + lag
            jt65_0_available(j,lag) = min(2,max(0,jz-k+1))
            jt65_0_row_count(lag) = jt65_0_row_count(lag) + jt65_0_available(j,lag)

            k = jsync1(j) + lag
            jt65_1_available(j,lag) = min(2,max(0,jz-k+1))
            jt65_1_row_count(lag) = jt65_1_row_count(lag) + jt65_1_available(j,lag)
         enddo
      enddo

      do i = 1, npol
         call pctile(savg(i, ia:ib), ib - ia + 1, 50, savg_med(i))
      enddo
!  do i=ia,ib
!     write(14,3014) 0.001*(i-1)*df3,savg(1:npol,i)
!3014 format(5f10.3)
!  enddo

      lagbest = 0
      ipolbest = 1
      flip = 0.

      do i = ia, ib
         ccfmax = 0.
         do lag = 0, LAGMAX

            nrows = q65_row_count(lag)
            if (nrows .ge. Q65_MIN_ROWS) then
               ccf = 0.
               ccf4 = 0.
               do j = 1, 22                        !Test for Q65 sync
                  k = isync(j) + lag
                  if (q65_available(j,lag) .ge. 1) &
                     ccf4(1:npol) = ccf4(1:npol) + ss(1:npol,k,i+1)
                  if (q65_available(j,lag) .ge. 2) &
                     ccf4(1:npol) = ccf4(1:npol) + ss(1:npol,k+1,i+1)
                  if (q65_available(j,lag) .ge. 3) &
                     ccf4(1:npol) = ccf4(1:npol) + ss(1:npol,k+2,i+1)
               enddo
               ccf4(1:npol) = ccf4(1:npol) - savg(1:npol,i+1)*real(nrows)/real(jz)
               row_scale = sqrt(real(Q65_SYNC_ROWS)/real(nrows))
               ccf4(1:npol) = row_scale*ccf4(1:npol)
               ccf = maxval(ccf4)
               ip = maxloc(ccf4)
               ipol = ip(1)
               if (ccf .gt. ccfmax) then
                  ipolbest = ipol
                  lagbest = lag
                  ccfmax = ccf
                  ccf4best = ccf4
                  flip = 0.
               endif
            endif

            nrows = jt65_0_row_count(lag)
            if (nrows .ge. JT65_MIN_ROWS) then
               ccf = 0.
               ccf4 = 0.
               do j = 1, 63                       !Test for JT65 sync, std msg
                  k = jsync0(j) + lag
                  if (jt65_0_available(j,lag) .ge. 1) &
                     ccf4(1:npol) = ccf4(1:npol) + ss(1:npol,k,i+1)
                  if (jt65_0_available(j,lag) .ge. 2) &
                     ccf4(1:npol) = ccf4(1:npol) + ss(1:npol,k+1,i+1)
               enddo
               ccf4(1:npol) = ccf4(1:npol) - savg(1:npol,i+1)*real(nrows)/real(jz)
               row_scale = sqrt(real(JT65_SYNC_ROWS)/real(nrows))
               ccf4(1:npol) = row_scale*ccf4(1:npol)
               ccf = maxval(ccf4)
               ip = maxloc(ccf4)
               ipol = ip(1)
               if (ccf .gt. ccfmax) then
                  ipolbest = ipol
                  lagbest = lag
                  ccfmax = ccf
                  ccf4best = ccf4
                  flip = 1.0
               endif
            endif

            nrows = jt65_1_row_count(lag)
            if (nrows .ge. JT65_MIN_ROWS) then
               ccf = 0.
               ccf4 = 0.
               do j = 1, 63                       !Test for JT65 sync, OOO msg
                  k = jsync1(j) + lag
                  if (jt65_1_available(j,lag) .ge. 1) &
                     ccf4(1:npol) = ccf4(1:npol) + ss(1:npol,k,i+1)
                  if (jt65_1_available(j,lag) .ge. 2) &
                     ccf4(1:npol) = ccf4(1:npol) + ss(1:npol,k+1,i+1)
               enddo
               ccf4(1:npol) = ccf4(1:npol) - savg(1:npol,i+1)*real(nrows)/real(jz)
               row_scale = sqrt(real(JT65_SYNC_ROWS)/real(nrows))
               ccf4(1:npol) = row_scale*ccf4(1:npol)
               ccf = maxval(ccf4)
               ip = maxloc(ccf4)
               ipol = ip(1)
               if (ccf .gt. ccfmax) then
                  ipolbest = ipol
                  lagbest = lag
                  ccfmax = ccf
                  ccf4best = ccf4
                  flip = -1.0
               endif
            endif

         enddo  ! lag

         poldeg = 0.
         if (xpol .and. ccfmax .ge. SNR1_THRESHOLD) then
            call polfit(ccf4best, 4, a)
            poldeg = a(3)
         endif
        sync(i)%ccfmax = ccfmax
        sync(i)%xdt    = lagbest*tstep - 1.0
        sync(i)%pol    = poldeg
        sync(i)%ipol   = ipolbest
        sync(i)%iflip  = int(flip)
        sync(i)%birdie = .false.
        if (ccfmax/(savg(ipolbest, i)/savg_med(ipolbest)) .lt. 3.0) sync(i)%birdie = .true.

        ! --- JT65 wideband probe (new) ---
!        freq_hz = 0.001 * (i - 1) * df3     ! convert FFT bin index to kHz

!        if (abs(freq_hz - 11.305d0) .lt. 0.02d0) then
!           write(*,*) 'WBSYNC BIN: freq=', freq_hz, &
!                      ' ccfmax=', sync(i)%ccfmax, &
!                      ' birdie=', sync(i)%birdie, &
!                      ' iflip=', sync(i)%iflip
!        endif

        enddo   ! i (frequency bin)

      call pctile(sync(ia:ib)%ccfmax, ib - ia + 1, 50, base)
      sync(ia:ib)%ccfmax = sync(ia:ib)%ccfmax/base

      ! A local-peak "collapse to single survivor" pass used to run here,
      ! blanking a wide (~450 Hz) same-type swath around whatever bin its
      ! own forward-only search happened to land on. Because that search
      ! and blanking radius were not centered on the true global peak, it
      ! could -- and, for closely-spaced real Q65 signals, did -- wipe out
      ! a much stronger genuine peak in favor of a weaker one found earlier
      ! in the frequency sweep, corrupting the candidate frequency reported
      ! for that signal (confirmed via w3sz debug logging: a real Q65
      ! signal's candidate frequency was off by ~150-200 Hz from its true
      ! peak whenever this pass ran, and exactly correct with it removed).
      ! get_candidates() below already does its own correct, strength-
      ! ordered, same-type-only deduplication (skip a candidate within its
      ! own signal bandwidth of an already-accepted stronger one), which
      ! does not have this failure mode, so this pass is not needed.

!  do i=ia,ib
!     write(15,3015) 0.001*(i-1)*df3+32.0,sync(i)%ccfmax,sync(i)%xdt,  &
!          sync(i)%ipol,sync(i)%iflip,sync(i)%birdie
!3015 format(3f10.3,2i6,L5)
!  enddo

      return
   end subroutine wb_sync

   subroutine init_wideband_sync()
      use npar_ptrs_mod, only: nfft_active
      implicit none
      if (.not. allocated(sync)) allocate (sync(nfft_active))
   end subroutine init_wideband_sync

end module wideband_sync
