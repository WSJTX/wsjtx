program test_readwav_validation
  use readwav, only: wav_header
  implicit none

  type(wav_header) :: wav
  integer :: samples_read, status, unit
  integer*2 :: samples(4)
  character(len=128) :: message

  call write_valid_wav('test_readwav_validation.wav')
  call wav%read('test_readwav_validation.wav', status, message)
  call assert_true(status == 0, 'valid WAV is accepted')
  call assert_true(wav%audio_format%sample_rate == 12000, 'format chunk is read')
  close(wav%lun)

  call write_format_wav('test_readwav_11025.wav', 1, 1, 11025, 16)
  call wav%read('test_readwav_11025.wav', status, message)
  call assert_true(status == 0, 'supported JT4 sample rate is accepted')
  close(wav%lun)

  call write_format_wav('test_readwav_stereo.wav', 1, 2, 12000, 16)
  call wav%read('test_readwav_stereo.wav', status, message)
  call assert_true(status /= 0, 'unsupported sample representation is rejected')

  call write_format_wav('test_readwav_48000.wav', 1, 1, 48000, 16)
  call wav%read('test_readwav_48000.wav', status, message)
  call assert_true(status /= 0, 'unsupported sample rate is rejected')

  call write_short_file('test_readwav_short.wav')
  call wav%read('test_readwav_short.wav', status, message)
  call assert_true(status /= 0, 'short WAV is rejected')

  call write_non_wave_file('test_readwav_not_wave.wav')
  call wav%read('test_readwav_not_wave.wav', status, message)
  call assert_true(status /= 0, 'non-WAVE RIFF file is rejected')

  call write_unpadded_odd_data('test_readwav_odd_data.wav')
  call wav%read('test_readwav_odd_data.wav', status, message)
  call assert_true(status == 0 .and. wav%data_size == 1, 'unpadded final data chunk is accepted')
  close(wav%lun)

  call write_wav_with_trailing_chunk('test_readwav_trailing_chunk.wav')
  call wav%read('test_readwav_trailing_chunk.wav', status, message)
  call assert_true(status == 0, 'WAV with trailing metadata is accepted')
  call wav%read_samples(samples, samples_read, status, message)
  call assert_true(status == 0 .and. samples_read == 1, 'only data chunk samples are read')
  call assert_true(samples(1) == 1234 .and. all(samples(2:) == 0), &
       'sample reads zero-fill without consuming metadata')
  call wav%read_samples(samples, samples_read, status, message)
  call assert_true(status == 0 .and. samples_read == 0, 'data chunk exhaustion is reported')
  close(wav%lun)

  call write_truncated_chunk('test_readwav_truncated.wav')
  call wav%read('test_readwav_truncated.wav', status, message)
  call assert_true(status /= 0, 'truncated chunk is rejected')

contains

  subroutine write_valid_wav(filename)
    character(len=*), intent(in) :: filename
    integer*2 :: audio_format, channels, block_align, bits_per_sample
    integer :: sample_rate, byte_rate

    audio_format = 1
    channels = 1
    sample_rate = 12000
    byte_rate = 24000
    block_align = 2
    bits_per_sample = 16
    open(newunit=unit, file=filename, access='stream', form='unformatted', status='replace')
    write(unit) 'RIFF', 36, 'WAVE', 'fmt ', 16, audio_format, channels, sample_rate, &
         byte_rate, block_align, bits_per_sample, 'data', 0
    close(unit)
  end subroutine write_valid_wav

  subroutine write_short_file(filename)
    character(len=*), intent(in) :: filename

    open(newunit=unit, file=filename, access='stream', form='unformatted', status='replace')
    write(unit) 'RIFF'
    close(unit)
  end subroutine write_short_file

  subroutine write_format_wav(filename, audio_format, channels, sample_rate, bits_per_sample)
    character(len=*), intent(in) :: filename
    integer, intent(in) :: audio_format, channels, sample_rate, bits_per_sample
    integer*2 :: format_value, channel_value, block_align, bits_value
    integer :: byte_rate

    format_value = audio_format
    channel_value = channels
    bits_value = bits_per_sample
    block_align = channels * bits_per_sample / 8
    byte_rate = sample_rate * block_align
    open(newunit=unit, file=filename, access='stream', form='unformatted', status='replace')
    write(unit) 'RIFF', 36, 'WAVE', 'fmt ', 16, format_value, channel_value, sample_rate, &
         byte_rate, block_align, bits_value, 'data', 0
    close(unit)
  end subroutine write_format_wav

  subroutine write_non_wave_file(filename)
    character(len=*), intent(in) :: filename

    open(newunit=unit, file=filename, access='stream', form='unformatted', status='replace')
    write(unit) 'RIFF', 36, 'NOPE'
    close(unit)
  end subroutine write_non_wave_file

  subroutine write_unpadded_odd_data(filename)
    character(len=*), intent(in) :: filename
    integer*2 :: audio_format, channels, block_align, bits_per_sample
    integer :: sample_rate, byte_rate

    audio_format = 1
    channels = 1
    sample_rate = 12000
    byte_rate = 24000
    block_align = 2
    bits_per_sample = 16
    open(newunit=unit, file=filename, access='stream', form='unformatted', status='replace')
    write(unit) 'RIFF', 37, 'WAVE', 'fmt ', 16, audio_format, channels, sample_rate, &
         byte_rate, block_align, bits_per_sample, 'data', 1, 'x'
    close(unit)
  end subroutine write_unpadded_odd_data

  subroutine write_truncated_chunk(filename)
    character(len=*), intent(in) :: filename

    open(newunit=unit, file=filename, access='stream', form='unformatted', status='replace')
    write(unit) 'RIFF', 36, 'WAVE', 'fmt ', 32
    close(unit)
  end subroutine write_truncated_chunk

  subroutine write_wav_with_trailing_chunk(filename)
    character(len=*), intent(in) :: filename
    integer*2 :: audio_format, channels, block_align, bits_per_sample, sample
    integer :: sample_rate, byte_rate

    audio_format = 1
    channels = 1
    sample_rate = 12000
    byte_rate = 24000
    block_align = 2
    bits_per_sample = 16
    sample = 1234
    open(newunit=unit, file=filename, access='stream', form='unformatted', status='replace')
    write(unit) 'RIFF', 50, 'WAVE', 'fmt ', 16, audio_format, channels, sample_rate, &
         byte_rate, block_align, bits_per_sample, 'data', 2, sample, 'JUNK', 4, 'abcd'
    close(unit)
  end subroutine write_wav_with_trailing_chunk

  subroutine assert_true(condition, text)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: text

    if (.not. condition) then
      write(*, '(A)') text
      error stop 1
    end if
  end subroutine assert_true

end program test_readwav_validation
