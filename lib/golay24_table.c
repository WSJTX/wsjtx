void golay24_table_(int cw[])
{
  // Compute arnd return the table of 4096 codewords for the Golay (24,12) code.

  // Array y contains the twelve rows (columns) of the parity-check matrix
  int y[12] = { 0x7ff, 0xee2, 0xdc5, 0xb8b, 0xf16, 0xe2d,
		0xc5b, 0x8b7, 0x96e, 0xadc, 0xdb8, 0xb71 };

  int c[2];          /* Codeword composed of 12-bit info and 12-bit parity */


  int i,j,k;
  int aux;
  int weight(int vector); 

  for(k=0; k<4096; k++) {
    c[0] = k;
    c[1] = 0;
    for (i=0; i<12; i++) {
      aux = 0;
      for (j=0; j<12; j++) {
	aux = aux ^ ((c[0] & y[i]) >> j & 1);
      }
      c[1] = (c[1] << 1) ^ aux;
    }
    cw[k]=4096*c[0] + c[1];
  }
}
