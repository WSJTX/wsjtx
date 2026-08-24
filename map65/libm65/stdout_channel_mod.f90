module stdout_channel_mod
    use iso_c_binding
    implicit none

    !===============================
    ! Types matching the C++ layout
    !===============================
    type, bind(C) :: StdoutSharedHeader
        integer(c_int32_t) :: version
        integer(c_int32_t) :: writeIndex
        integer(c_int32_t) :: readIndex
        integer(c_int32_t) :: seq
    end type StdoutSharedHeader

    !===============================
    ! Internal state
    !===============================
    type(c_ptr)                 :: g_buf_ptr        = c_null_ptr
    type(c_ptr)                 :: g_hdr_ptr        = c_null_ptr
    integer(c_int32_t)          :: g_buf_size       = 0
    integer(c_intptr_t)         :: g_event_handle   = 0

    type(StdoutSharedHeader), pointer :: hdr => null()
    character(kind=c_char), dimension(:), pointer :: buf => null()

    logical :: initialized = .false.

    !===============================
    ! Win32 SetEvent
    !===============================
    interface
        function win_set_event(hEvent) bind(C, name="win_set_event")
            use iso_c_binding
            implicit none
            type(c_ptr), value :: hEvent
            integer(c_int)     :: win_set_event
        end function win_set_event
    end interface

contains

    !=========================================================
    ! Called from C++ before run_m65_ starts
    !=========================================================
subroutine set_stdout_channel(buf_ptr, hdr_ptr, buf_size, event_handle) &
        bind(C, name="set_stdout_channel")

    use iso_c_binding
    implicit none

    !==== Dummy arguments =====================================================
    type(c_ptr),        value, intent(in) :: buf_ptr
    type(c_ptr),        value, intent(in) :: hdr_ptr
    integer(c_int32_t), value, intent(in) :: buf_size
    integer(c_intptr_t),value, intent(in) :: event_handle

    !==== Body ================================================================
    if (c_associated(buf_ptr) .and. c_associated(hdr_ptr) .and. buf_size > 0) then
        g_buf_ptr      = buf_ptr
        g_hdr_ptr      = hdr_ptr
        g_buf_size     = buf_size
        g_event_handle = event_handle

        call c_f_pointer(hdr_ptr, hdr)
        call c_f_pointer(buf_ptr, buf, [buf_size])

        initialized = .true.
    else
        initialized = .false.
    end if
end subroutine set_stdout_channel

    !=========================================================
    ! Safe write into shared memory + event signal
    !=========================================================
subroutine write_stdout(msg)
    use iso_c_binding
    use sleep_msec_mod, only: sleep_msec
    implicit none

    !==== Dummy argument =====================================================
    character(len=*), intent(in) :: msg

    !==== Local variables ====================================================
    integer(c_int32_t) :: n, i, rc
    integer(c_int32_t) :: w, idx, r, occupied, free_bytes
    integer :: nwait
    integer, parameter :: MAX_WAIT_ITERS = 2000  ! ~2 s at 1 ms/iter
    character(kind=c_char) :: ch

    if (.not. initialized) return
    if (.not. associated(hdr)) return
    if (.not. associated(buf)) return

    ! Length of the message (assumed to include newline)
    n = len_trim(msg)
    if (n <= 0) return

    ! Avoid filling the entire buffer
    if (n >= g_buf_size) n = g_buf_size - 1

    ! Snapshot current write index (0..g_buf_size-1)
    w = hdr%writeIndex
    if (w < 0 .or. w >= g_buf_size) w = 0

    ! Wait for the reader to make room rather than overwriting unread
    ! data. One byte of the buffer is always kept empty so a full ring
    ! can be told apart from an empty one. If the reader stalls for a
    ! couple of seconds we give up waiting and write anyway, so a dead
    ! or stuck reader can never hang the decoder.
    do nwait = 1, MAX_WAIT_ITERS
        r = hdr%readIndex
        if (r < 0 .or. r >= g_buf_size) r = 0
        occupied = w - r
        if (occupied < 0) occupied = occupied + g_buf_size
        free_bytes = g_buf_size - 1 - occupied
        if (free_bytes >= n) exit
        call sleep_msec(1)
    end do

    ! Write bytes with wraparound, using 0-based idx and 1-based buf
    do i = 0, n - 1
        idx = w + i
        if (idx >= g_buf_size) idx = idx - g_buf_size
        ch = msg(i+1:i+1)
        buf(idx+1) = ch    ! buf is 1-based; idx is 0-based
    end do

    ! Advance writeIndex to first free slot after the data
    w = w + n
    if (w >= g_buf_size) w = w - g_buf_size
    hdr%writeIndex = w

    ! Optional: bump seq
    hdr%seq = hdr%seq + 1

    rc = win_set_event(transfer(g_event_handle, c_null_ptr))
end subroutine write_stdout

end module stdout_channel_mod

