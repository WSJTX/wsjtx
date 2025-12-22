void ldpc32_table_(int cw[])
{
  // Compute and return the table of 65535 codewords for the (32,16) code.

  // Array y contains the sixteen rows (columns) of the parity-check matrix
  int y[16] = { 0xd452, 0x7ecb, 0xc5d5, 0xf81c, 
                0x61d7, 0x0ed8, 0xa3c5, 0x9ef9,
                0xb3bd, 0xe5b6, 0x2fcd, 0xc23a,
                0x5deb, 0xfa0e, 0x35fc, 0x1379 };

  unsigned int c[2];          /* Codeword composed of 16-bit info and 16-bit parity */


  int i,j,k;
  int aux;
  int weight(int vector); 

  for(k=0; k<65536; k++) {
    c[0] = k;
    c[1] = 0;
    for (i=0; i<16; i++) {
      aux = 0;
      for (j=0; j<16; j++) {
	aux = aux ^ ((c[0] & y[i]) >> j & 1);
      }
      c[1] = (c[1] << 1) ^ aux;
    }
    cw[k]=65536*c[1] + c[0];
  }
}
