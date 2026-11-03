! SPDX-License-Identifier: GPL-3.0-or-later
module test_streaming_emit_progress_fixture
  use streaming_emit, only: streaming_emit_progress
  implicit none
contains
  subroutine emit_contended_progress() bind(C)
    call streaming_emit_progress(8, '111111', 49)
  end subroutine emit_contended_progress
end module test_streaming_emit_progress_fixture

program test_streaming_emit_progress
  use, intrinsic :: iso_c_binding, only: c_int
  use streaming_emit, only: streaming_emit_set_enabled, streaming_emit_set_writer, &
       streaming_emit_reply, streaming_emit_line, streaming_emit_progress
  implicit none
  interface
    function test_writer_start() bind(C) result(ok)
      import c_int
      integer(c_int) :: ok
    end function test_writer_start
    subroutine test_writer_finish() bind(C)
    end subroutine test_writer_finish
    function test_progress_start() bind(C) result(ok)
      import c_int
      integer(c_int) :: ok
    end function test_progress_start
    function test_progress_wait() bind(C) result(ok)
      import c_int
      integer(c_int) :: ok
    end function test_progress_wait
    function test_progress_join() bind(C) result(ok)
      import c_int
      integer(c_int) :: ok
    end function test_progress_join
    subroutine stream_writer_lock() bind(C)
    end subroutine stream_writer_lock
    function stream_writer_trylock() bind(C) result(ok)
      import c_int
      integer(c_int) :: ok
    end function stream_writer_trylock
    subroutine stream_writer_unlock() bind(C)
    end subroutine stream_writer_unlock
  end interface
  integer(c_int) :: started, completed, joined

  if (test_writer_start() .eq. 0) error stop 'failed to initialize writer'
  call streaming_emit_set_writer(stream_writer_lock, stream_writer_trylock, stream_writer_unlock)
  call streaming_emit_set_enabled(.true.)
  call streaming_emit_reply(.true.)
  call streaming_emit_line('{"t":"reply-start"}')
  started = test_progress_start()
  completed = 0
  if (started .ne. 0) completed = test_progress_wait()
  call streaming_emit_line('{"t":"reply-end"}')
  call streaming_emit_reply(.false.)
  joined = 0
  if (started .ne. 0) joined = test_progress_join()
  if (completed .eq. 0 .or. joined .eq. 0) error stop 'progress did not return while writer was held'
  call streaming_emit_progress(8, '222222', 49)
  call test_writer_finish()
end program test_streaming_emit_progress
