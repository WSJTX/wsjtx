module q65_codec
  use iso_c_binding
  implicit none
  private
  public :: q65_codec_create, q65_codec_destroy, select_q65_codec, q65_codec_status
  public :: q65_enc, q65_intrinsics_ff, q65_dec, q65_dec_fullaplist

  ! Codec selection is scoped to one serialized decoder operation on its thread.
  type(c_ptr), save :: selected_codec=c_null_ptr
  integer, save :: codec_status=0
  !$omp threadprivate(selected_codec,codec_status)

  interface
    function q65_codec_create() bind(C) result(handle)
      import c_ptr
      type(c_ptr) :: handle
    end function
    subroutine q65_codec_destroy(handle) bind(C)
      import c_ptr
      type(c_ptr), value :: handle
    end subroutine
    integer(c_int) function codec_encode(handle,x,y) bind(C,name="q65_codec_encode")
      import c_ptr,c_int
      type(c_ptr), value :: handle
      integer(c_int), intent(in) :: x(*)
      integer(c_int), intent(out) :: y(*)
    end function
    integer(c_int) function codec_intrinsics(handle,s3,submode,b90ts,fading,prob) &
        bind(C,name="q65_codec_intrinsics")
      import c_ptr,c_int,c_float
      type(c_ptr), value :: handle
      real(c_float), intent(in) :: s3(*)
      integer(c_int), value :: submode,fading
      real(c_float), value :: b90ts
      real(c_float), intent(out) :: prob(*)
    end function
    integer(c_int) function codec_decode(handle,s3,prob,mask,symbols,maxiters,esnodb,xdec,rc) &
        bind(C,name="q65_codec_decode")
      import c_ptr,c_int,c_float
      type(c_ptr), value :: handle
      real(c_float), intent(in) :: s3(*),prob(*)
      integer(c_int), intent(in) :: mask(*),symbols(*)
      integer(c_int), value :: maxiters
      real(c_float), intent(out) :: esnodb
      integer(c_int), intent(out) :: xdec(*),rc
    end function
    integer(c_int) function codec_fullaplist(handle,s3,prob,words,ncw,esnodb,xdec,plog,rc) &
        bind(C,name="q65_codec_decode_fullaplist")
      import c_ptr,c_int,c_float
      type(c_ptr), value :: handle
      real(c_float), intent(in) :: s3(*),prob(*)
      integer(c_int), intent(in) :: words(*)
      integer(c_int), value :: ncw
      real(c_float), intent(out) :: esnodb,plog
      integer(c_int), intent(out) :: xdec(*),rc
    end function
    subroutine legacy_encode(x,y) bind(C,name="q65_enc_")
      import c_int
      integer(c_int), intent(in) :: x(*)
      integer(c_int), intent(out) :: y(*)
    end subroutine
    subroutine legacy_intrinsics(s3,submode,b90ts,fading,prob) bind(C,name="q65_intrinsics_ff_")
      import c_int,c_float
      real(c_float), intent(in) :: s3(*),b90ts
      integer(c_int), intent(in) :: submode,fading
      real(c_float), intent(out) :: prob(*)
    end subroutine
    subroutine legacy_decode(s3,prob,mask,symbols,maxiters,esnodb,xdec,rc) bind(C,name="q65_dec_")
      import c_int,c_float
      real(c_float), intent(in) :: s3(*),prob(*)
      integer(c_int), intent(in) :: mask(*),symbols(*),maxiters
      real(c_float), intent(out) :: esnodb
      integer(c_int), intent(out) :: xdec(*),rc
    end subroutine
    subroutine legacy_fullaplist(s3,prob,words,ncw,esnodb,xdec,plog,rc) bind(C,name="q65_dec_fullaplist_")
      import c_int,c_float
      real(c_float), intent(in) :: s3(*),prob(*)
      integer(c_int), intent(in) :: words(*),ncw
      real(c_float), intent(out) :: esnodb,plog
      integer(c_int), intent(out) :: xdec(*),rc
    end subroutine
  end interface

contains

  subroutine select_q65_codec(handle)
    type(c_ptr), intent(in) :: handle
    selected_codec=handle
    codec_status=0
  end subroutine

  integer function q65_codec_status()
    q65_codec_status=codec_status
  end function

  ! With codec, that codec (null: the legacy one), whatever this thread selected.
  subroutine q65_enc(x,y,codec)
    integer, intent(in) :: x(13)
    integer, intent(out) :: y(63)
    type(c_ptr), intent(in), optional :: codec
    y=0
    if(present(codec)) then
      if(.not.c_associated(codec)) then
        call legacy_encode(x,y)
      else if(codec_encode(codec,x,y)/=0) then
        y=0
      endif
      return
    endif
    if(codec_status/=0) return
    if(c_associated(selected_codec)) then
      codec_status=codec_encode(selected_codec,x,y)
    else
      call legacy_encode(x,y)
    endif
  end subroutine

  subroutine q65_intrinsics_ff(s3,submode,b90ts,fading,prob)
    real, intent(in) :: s3(*)
    integer, intent(in) :: submode,fading
    real, intent(in) :: b90ts
    real, intent(out) :: prob(64,63)
    prob=0
    if(codec_status/=0) return
    if(c_associated(selected_codec)) then
      codec_status=codec_intrinsics(selected_codec,s3,submode,b90ts,fading,prob)
    else
      call legacy_intrinsics(s3,submode,b90ts,fading,prob)
    endif
  end subroutine

  subroutine q65_dec(s3,prob,mask,symbols,maxiters,esnodb,xdec,rc)
    real, intent(in) :: s3(*),prob(64,63)
    integer, intent(in) :: mask(13),symbols(13),maxiters
    real, intent(out) :: esnodb
    integer, intent(out) :: xdec(13),rc
    esnodb=0
    xdec=0
    rc=-1
    if(codec_status/=0) return
    if(c_associated(selected_codec)) then
      codec_status=codec_decode(selected_codec,s3,prob,mask,symbols,maxiters,esnodb,xdec,rc)
    else
      call legacy_decode(s3,prob,mask,symbols,maxiters,esnodb,xdec,rc)
    endif
  end subroutine

  subroutine q65_dec_fullaplist(s3,prob,words,ncw,esnodb,xdec,plog,rc)
    real, intent(in) :: s3(*),prob(64,63)
    integer, intent(in) :: words(*),ncw
    real, intent(out) :: esnodb,plog
    integer, intent(out) :: xdec(13),rc
    esnodb=0
    plog=0
    xdec=0
    rc=-1
    if(codec_status/=0) return
    if(c_associated(selected_codec)) then
      codec_status=codec_fullaplist(selected_codec,s3,prob,words,ncw,esnodb,xdec,plog,rc)
    else
      call legacy_fullaplist(s3,prob,words,ncw,esnodb,xdec,plog,rc)
    endif
  end subroutine

end module q65_codec
