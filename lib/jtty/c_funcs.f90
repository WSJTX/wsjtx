MODULE c_funcs

  USE, INTRINSIC :: ISO_C_BINDING
    IMPLICIT NONE

    INTERFACE
        ! Use BIND(C, name="putc") to link to the C library function
        FUNCTION c_putc(character) BIND(C, name="putchar")
            INTEGER(C_INT) :: c_putc
        END FUNCTION c_putc
    END INTERFACE

  END MODULE C_Funcs
