#include <windows.h>
#include <stdio.h>

int ptt_(int *nport, int *ntx, int *iptt)
{
  static HANDLE hFile;
  static int open=0;
  char s[10];
  int i3=0,i4=0,i5=0,i6=0,i9=0,i00=0;

  if(*nport==0) {
    *iptt=*ntx;
    return(0);
  }

  if(*ntx && (!open)) {
    sprintf(s,"COM%d",*nport);
    hFile=CreateFile(TEXT(s),GENERIC_WRITE,0,NULL,OPEN_EXISTING,
		     FILE_ATTRIBUTE_NORMAL,NULL);
    if(hFile==INVALID_HANDLE_VALUE) {
      //      printf("PTT: Cannot open COM port %d.\n",*nport);
      return 1;
    }
    open=1;
  }

  if(*ntx && open) {
    EscapeCommFunction(hFile,3);
    EscapeCommFunction(hFile,5);
    *iptt=1;
  }

  else {
    EscapeCommFunction(hFile,4);
    EscapeCommFunction(hFile,6);
    EscapeCommFunction(hFile,9);
    i00=CloseHandle(hFile);
    *iptt=0;
    open=0;
  }
  if((i00+i3+i4+i5+i6+i9)==-99) return -1;  //Silence compiler warning
  return 0;
}
