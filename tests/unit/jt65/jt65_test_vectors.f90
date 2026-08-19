module jt65_test_vectors
  implicit none

  integer, parameter :: jt65_symbol_count = 126

  integer, parameter :: standard_tones(jt65_symbol_count) = [ &
       0, 3, 62, 0, 0, 50, 13, 40, 0, 0, 0, 0, &
       0, 0, 23, 0, 52, 0, 10, 39, 13, 0, 15, 0, &
       0, 3, 58, 0, 26, 47, 63, 0, 0, 0, 62, 21, &
       0, 0, 0, 0, 20, 0, 0, 4, 0, 0, 0, 0, &
       21, 24, 60, 0, 0, 56, 0, 46, 0, 37, 0, 0, &
       46, 30, 0, 0, 30, 0, 53, 0, 13, 0, 6, 17, &
       0, 62, 42, 59, 31, 44, 17, 0, 0, 56, 43, 48, &
       8, 42, 49, 40, 0, 0, 59, 0, 15, 3, 0, 37, &
       0, 0, 31, 0, 20, 0, 52, 0, 51, 35, 0, 0, &
       20, 19, 0, 10, 9, 0, 59, 56, 52, 62, 0, 0, &
       0, 0, 0, 0, 0, 0 ]

  integer, parameter :: ooo_tones(jt65_symbol_count) = [ &
       3, 0, 0, 62, 50, 0, 0, 0, 13, 40, 23, 52, &
       10, 39, 0, 13, 0, 15, 0, 0, 0, 3, 0, 58, &
       26, 0, 0, 47, 0, 0, 0, 63, 62, 21, 0, 0, &
       20, 4, 21, 24, 0, 60, 56, 0, 46, 37, 46, 30, &
       0, 0, 0, 30, 53, 0, 13, 0, 6, 0, 17, 62, &
       0, 0, 42, 59, 0, 31, 0, 44, 0, 17, 0, 0, &
       56, 0, 0, 0, 0, 0, 0, 43, 48, 0, 0, 0, &
       0, 0, 0, 0, 8, 42, 0, 49, 0, 0, 40, 0, &
       59, 15, 0, 3, 0, 37, 0, 31, 0, 0, 20, 52, &
       0, 0, 51, 0, 0, 35, 0, 0, 0, 0, 20, 19, &
       10, 9, 59, 56, 52, 62 ]

contains

  pure function padded_message(text) result(message)
    character(len=*), intent(in) :: text
    character(len=22) :: message

    message = '                      '
    message(1:min(len_trim(text), len(message))) = &
         text(1:min(len_trim(text), len(message)))
  end function padded_message

end module jt65_test_vectors
