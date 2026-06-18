module gran_interface
  use iso_c_binding
  implicit none

  interface
    function gran() bind(C, name='gran_')
      import :: c_float
      real(c_float) :: gran
    end function gran

    function rran() bind(C, name='rran_')
      import :: c_float
      real(c_float) :: rran
    end function rran
  end interface

end module gran_interface
