subroutine jtty_block_pow(z, nsym, iblock, pow_block)

! Coherent block detection for JTTY's 4-FSK symbols. Cascades the already-
! computed blocksize-1 complex correlator outputs z(0:3,1:nsym) (see
! jtty_mdecode.f90's decode_and_merge) into coherent blocksize-2 and, if
! iblock==4, blocksize-4 candidate-sequence sums, then reduces back to a
! max-over-consistent-sequences per-tone/per-position array shaped like the
! ordinary single-symbol pow(0:3,1:nsym), ready to feed directly into
! tbcc_wava_fsk_decode's tone_energies argument -- no changes needed there.
!
! Combining operator is plain addition at every cascade level, with no
! rotation/derotation term. This is specific to JTTY's tone grid: ctones()
! is built with dphi=i*TWOPI/nss, so one full symbol window advances tone
! i's phase by exactly i*TWOPI -- an exact multiple of 2*pi for all four
! tones -- unlike e.g. FST4's centered tone grid (get_fst4_bitmetrics.f90),
! which picks up a guaranteed 180-degree flip every symbol and so needs a
! subtraction at its first cascade step.
!
! nsym is expected even (NCHAN_SYM=46). The L=2 cascade (nsym/2 pairs) is
! always built first and used directly when iblock==2. When iblock==4,
! pairs are grouped by two; if the pair count is odd, the last pair is left
! at its L=2 reduction instead of being folded into an L=4 group -- this
! costs nothing extra, since the L=2 cascade already covers every position.

  implicit none
  integer, intent(in)  :: nsym          ! NCHAN_SYM (46)
  integer, intent(in)  :: iblock        ! 2 or 4
  complex, intent(in)  :: z(0:3,nsym)
  real,    intent(out) :: pow_block(0:3,nsym)

  complex :: c2(0:3,0:3,nsym/2)
  complex :: s4
  integer :: npair, nquad, ip, iq, ipL, ipR
  integer :: posA, posB, pos1, pos2, pos3, pos4
  integer :: ta, tb, tc, td
  real    :: p2

  npair = nsym/2

  do ip=1,npair
     posA=2*ip-1
     posB=2*ip
     do ta=0,3
        do tb=0,3
           c2(ta,tb,ip) = z(ta,posA) + z(tb,posB)
        enddo
     enddo
  enddo

! L=2 reduction, always computed: used directly for iblock==2, and as the
! trailing-remainder fallback for iblock==4.
  do ip=1,npair
     posA=2*ip-1
     posB=2*ip
     do ta=0,3
        p2 = real(c2(ta,0,ip)*conjg(c2(ta,0,ip)))
        p2 = max(p2, real(c2(ta,1,ip)*conjg(c2(ta,1,ip))))
        p2 = max(p2, real(c2(ta,2,ip)*conjg(c2(ta,2,ip))))
        p2 = max(p2, real(c2(ta,3,ip)*conjg(c2(ta,3,ip))))
        pow_block(ta,posA) = p2/2.0
     enddo
     do tb=0,3
        p2 = real(c2(0,tb,ip)*conjg(c2(0,tb,ip)))
        p2 = max(p2, real(c2(1,tb,ip)*conjg(c2(1,tb,ip))))
        p2 = max(p2, real(c2(2,tb,ip)*conjg(c2(2,tb,ip))))
        p2 = max(p2, real(c2(3,tb,ip)*conjg(c2(3,tb,ip))))
        pow_block(tb,posB) = p2/2.0
     enddo
  enddo

  if(iblock.eq.4) then
     nquad = npair/2
     do iq=1,nquad
        ipL=2*iq-1
        ipR=2*iq
        pos1=2*ipL-1
        pos2=2*ipL
        pos3=2*ipR-1
        pos4=2*ipR
        pow_block(:,pos1)=0.0
        pow_block(:,pos2)=0.0
        pow_block(:,pos3)=0.0
        pow_block(:,pos4)=0.0
        do ta=0,3
           do tb=0,3
              do tc=0,3
                 do td=0,3
                    s4 = c2(ta,tb,ipL) + c2(tc,td,ipR)
                    p2 = real(s4*conjg(s4))/4.0
                    pow_block(ta,pos1) = max(pow_block(ta,pos1), p2)
                    pow_block(tb,pos2) = max(pow_block(tb,pos2), p2)
                    pow_block(tc,pos3) = max(pow_block(tc,pos3), p2)
                    pow_block(td,pos4) = max(pow_block(td,pos4), p2)
                 enddo
              enddo
           enddo
        enddo
     enddo
  endif

end subroutine jtty_block_pow
