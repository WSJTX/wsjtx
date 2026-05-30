# JTTY transmit manual checks

These checks cover MainWindow, PTT, and backend behavior that is outside the
FIFO unit-test boundary.

- Soundcard: send one JTTY message and verify the full tail plays before PTT drops.
- Soundcard: queue two or three messages rapidly and verify playback is contiguous with PTT held.
- Soundcard: press Stop during playback and verify queued audio is abandoned before PTT drops.
- Soundcard: press Esc before PTT comes up and during playback and verify clean aborts.
- Soundcard: send again after a natural drain and verify a fresh transmit session starts.
- TCI, where available: repeat the same checks and watch for backend enqueue failure warnings.
