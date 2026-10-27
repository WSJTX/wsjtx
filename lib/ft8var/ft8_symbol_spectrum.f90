module ft8_symbol_spectrum
  use ft8_fft32, only: fft32_first_eight
  implicit none
  private
  public :: ft8_symbol_spectra
  public :: ft8_reverse_spectrum

  complex, parameter :: reverse_phase(0:7) = [ &
       (1.0,0.0), (0.9807852804,0.1950903220), &
       (0.9238795325,0.3826834324), (0.8314696123,0.5555702330), &
       (0.7071067812,0.7071067812), (0.5555702330,0.8314696123), &
       (0.3826834324,0.9238795325), (0.1950903220,0.9807852804) ]

contains

  pure subroutine ft8_symbol_spectra(symbol,forward,reversed)
    complex, intent(in) :: symbol(32)
    complex, intent(out) :: forward(0:7),reversed(0:7)

    call fft32_first_eight(symbol,forward)
    call ft8_reverse_spectrum(forward,reversed)
  end subroutine ft8_symbol_spectra

  pure subroutine ft8_reverse_spectrum(forward,reversed)
    complex, intent(in) :: forward(0:7)
    complex, intent(out) :: reversed(0:7)

    ! DFT(conjg(x(31-n)))[k] = exp(2*pi*i*k/32)*conjg(DFT(x)[k]).
    reversed=reverse_phase*conjg(forward)
  end subroutine ft8_reverse_spectrum
end module ft8_symbol_spectrum
