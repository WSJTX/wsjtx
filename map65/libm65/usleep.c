#include <unistd.h>

/* usleep(3) */
void usleep_(unsigned long *microsec)
{
  usleep(*microsec);
}
