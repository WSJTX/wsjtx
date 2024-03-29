subroutine ftrsd3(s3,rxdat,rxprob,rxdat2,rxprob2,ntrials0,correct,param,ntry)

! Soft-decision decoder for Reed-Solomon codes.
 
! This decoding scheme is built around Phil Karn's Berlekamp-Massey
! errors and erasures decoder. The approach is inspired by a number of
! publications, including the stochastic Chase decoder described
! in "Stochastic Chase Decoding of Reed-Solomon Codes", by Leroux et al.,
! IEEE Communications Letters, Vol. 14, No. 9, September 2010 and
! "Soft-Decision Decoding of Reed-Solomon Codes Using Successive Error-
! and-Erasure Decoding," by Soo-Woong Lee and B. V. K. Vijaya Kumar.

! Steve Franke K9AN and Joe Taylor K1JT

  use sfox_mod

  real s3(0:NQ-1,0:NN-1)          !Symbol spectra
  integer rxdat(0:NN-1)           !Hard-decision symbol values
  integer rxprob(0:NN-1)          !Probabilities that rxdat values are correct
  integer rxdat2(0:NN-1)          !Second most probable symbol values
  integer rxprob2(0:NN-1)         !Probabilities that rxdat2 values are correct
  integer workdat(0:NN-1)         !Work array
  integer correct(0:NN-1)         !Corrected codeword
  integer indexes(0:NN-1)         !For sorting probabilities
  integer probs(0:NN-1)           !Temp array for sorting probabilities
  integer thresh0(0:NN-1)         !Temp array for thresholds
  integer era_pos(0:NN-KK-1)      !Index values for erasures
  integer param(0:8)
  integer*8 nseed,ir              !No unsigned int in Fortran
  integer pass,tmp,thresh
  
  integer perr(0:7,0:7)
  data perr/ 4, 9,11,13,14,14,15,15, &
             2,20,20,30,40,50,50,50, &
             7,24,27,40,50,50,50,50, &
            13,25,35,46,52,70,50,50, &
            17,30,42,54,55,64,71,70, &
            25,39,48,57,64,66,77,77, &
            32,45,54,63,66,75,78,83, &
            51,58,57,66,72,77,82,86/
  
  ntrials=ntrials0
  nhard=0
  nhard_min=32768
  nsoft=0
  nsoft_min=32768
  ntotal=0
  ntotal_min=32768
  nera_best=0
  nsym=nn

  do i=0,NN-1
     indexes(i)=i
     probs(i)=rxprob(i)
  enddo

  do pass=1,nsym-1
     do k=0,nsym-pass-1
        if(probs(k).lt.probs(k+1)) then
           tmp=probs(k)
           probs(k)=probs(k+1)
           probs(k+1)=tmp
           tmp=indexes(k)
           indexes(k)=indexes(k+1)
           indexes(k+1)=tmp
        endif
     enddo
  enddo

  correct=-1
  era_pos=0
  numera=0
  workdat=rxdat
  call rs_decode_sf(workdat,era_pos,numera,nerr)    !Call the decoder
  nerr=-1

  if(nerr.ge.0) then
! Hard-decision decoding succeeded.  Save codeword and some parameters.
     nhard=count(workdat.ne.rxdat)
     correct=workdat
     param(0)=0
     param(1)=nhard
     param(2)=0
     param(3)=0
     param(4)=0
     param(5)=0
     param(7)=1000*1000               !???
     ntry=0
     go to 900
  endif

! Hard-decision decoding failed.  Try the FT soft-decision method.
! Generate random erasure-locator vectors and see if any of them
! decode. This will generate a list of "candidate" codewords.  The
! soft distance between each candidate codeword and the received 
! word is estimated by finding the largest (pp1) and second-largest 
! (pp2) outputs from a synchronized filter-bank operating on the 
! symbol spectra, and using these to decide which candidate 
! codeword is "best".  

  nseed=1                             !Seed for random numbers
  ncandidates=0
  nsum=0
  do i=0,NN-1
     nsum=nsum+rxprob(i)
     j=indexes(NN-1-i)
     ratio=float(rxprob2(j))/(float(rxprob(j))+0.01)
     ii=7.999*ratio
     jj=int((7.999/NN)*(NN-1-i))
     thresh0(i)=1.15*perr(jj,ii)
  enddo
  if(nsum.le.0) return

  pp1=0.
  pp2=0.
  do k=1,ntrials
     era_pos=0
     workdat=rxdat

! Mark a subset of the symbols as erasures.
! Run through the ranked symbols, starting with the worst, i=0.
! NB: j is the symbol-vector index of the symbol with rank i.

     numera=0
     do i=0,NN-1
        j=indexes(NN-1-i)
        thresh=thresh0(i)
! Generate a random number ir, 0 <= ir <= 100 (see POSIX.1-2001 example).
        ir=100.0*ran1(nseed)
        if((ir.lt.thresh) .and. numera.lt. 0.69*(NN-KK)) then
           era_pos(numera)=j
           numera=numera+1
        endif
     enddo
     call rs_decode_sf(workdat,era_pos,numera,nerr)    !Call the decoder
     if( nerr.ge.0) then
      ! We have a candidate codeword.  Find its hard and soft distance from
      ! the received word.  Also find pp1 and pp2 from the full array 
      ! s3(NQ,NN) of synchronized symbol spectra.
        ncandidates=ncandidates+1
        nhard=0
        nsoft=0
        do i=0,NN-1
           if(workdat(i).ne. rxdat(i)) then
              nhard=nhard+1;
              if(workdat(i) .ne. rxdat2(i)) nsoft=nsoft+rxprob(i)
           endif
        enddo
        nsoft=NN*nsoft/nsum
        ntotal=nsoft+nhard

        pp=0.
        call getpp3(s3,workdat,pp)
!        write(*,5001) ncandidates,nhard,nsoft,ntotal,pp,pp1,pp2
!5001    format(4i8,3f7.3)
        if(pp.gt.pp1) then
           pp2=pp1
           pp1=pp
           nsoft_min=nsoft
           nhard_min=nhard
           ntotal_min=ntotal
           correct=workdat
           nera_best=numera
           ntry=k
        else
           if(pp.gt.pp2 .and. pp.ne.pp1) pp2=pp
        endif
        if(nhard_min.le.60 .and. ntotal_min.le.90) exit   !### Needs tuning
     endif
     if(k.eq.ntrials) ntry=k
  enddo

  param(0)=ncandidates
  param(1)=nhard_min
  param(2)=nsoft_min
  param(3)=nera_best
  param(4)=1000
  if(pp1.gt.0.0) param(4)=1000.0*pp2/pp1
  param(5)=ntotal_min
  param(6)=ntry
  param(7)=1000.0*pp2
  param(8)=1000.0*pp1
  if(param(0).eq.0) param(2)=-1

900 return
end subroutine ftrsd3
