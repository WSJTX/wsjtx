/*
 * WSJT is Copyright (c) 2001-2006 by Joseph H. Taylor, Jr., K1JT, 
 * and is licensed under the GNU General Public License (GPL).
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
# include <stdio.h>
#  include <stdlib.h>
# include <unistd.h>
#include <fcntl.h>
# include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>

static void ptt_log(const char *fmt, ...)
{
    FILE *f = fopen("/tmp/map65_ptt.log", "a");
    if (!f) return;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    fprintf(f, "\n");
    va_end(ap);
    fflush(f);
    fclose(f);
}

#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#if (defined(__unix__) || defined(unix)) && !defined(USG)
# include <sys/param.h>
#endif

#include <string.h>

int ptt_serial(int fd, int *ntx, int *iptt);

/*
 * ptt_
 *
 * generic unix PTT routine called from Fortran
 *
 * Inputs	
 * unused	Unused, to satisfy old windows calling convention
 * ptt_port	device name
 * ntx		pointer to fortran command on or off
 * iptt		pointer to fortran command status on or off
 * Returns	- non 0 if error
*/

static char ptt_override[256] = {0};
static int ptt_override_valid = 0;

void ptt_set_override(const char *path)
{
    if (path && *path) {
        strncpy(ptt_override, path, sizeof(ptt_override)-1);
        ptt_override_valid = 1;
    } else {
        ptt_override_valid = 0;
    }
}

static int fd = -1;

int ptt_(int *nport, int *ntx, int *iptt)
{
	(void)nport;
//    ptt_log("ptt_unix: entry nport=%d ntx=%d iptt=%d", *nport, *ntx, *iptt);

    // PTT disabled
    if (!ptt_override_valid) {
    *iptt=*ntx;
    return 0;
  }

    const char *ptt_port = ptt_override;

    // Open once, keep open
    if (fd < 0) {
        fd = open(ptt_port, O_RDWR | O_NONBLOCK);
        if (fd < 0) {
//        ptt_log("ptt_unix: open failed errno=%d (%s)", errno, strerror(errno));
            return 1;
  }
//    ptt_log("ptt_unix: open OK fd=%d", fd);

    // *** PATCH: Force RTS+DTR LOW immediately after open ***
    int status = 0;
    if (ioctl(fd, TIOCMGET, &status) == 0) {
        status &= ~(TIOCM_RTS | TIOCM_DTR);
        ioctl(fd, TIOCMSET, &status);
//        ptt_log("ptt_unix: forced RTS/DTR LOW after open, status=0x%x", status);
    }
    }
      ptt_serial(fd, ntx, iptt);
    return 0;
    }


/*
 * ptt_serial
 *
 * generic serial unix PTT routine called indirectly from Fortran
 *
 * fd		- already opened file descriptor
 * ntx		- pointer to fortran command on or off
 * iptt		- pointer to fortran command status on or off
 */


int
ptt_serial(int fd, int *ntx, int *iptt)
{
int status;

if (ioctl(fd, TIOCMGET, &status) < 0) {
//    ptt_log("TIOCMGET failed errno=%d (%s)", errno, strerror(errno));
    return 1;
}

  if(*ntx) {
    status |= (TIOCM_RTS | TIOCM_DTR);   // PTT ON
  } else {
    status &= ~(TIOCM_RTS | TIOCM_DTR);  // PTT OFF
}

if (ioctl(fd, TIOCMSET, &status) < 0) {
//    ptt_log("TIOCMSET failed errno=%d (%s)", errno, strerror(errno));
    return 1;
}

//ptt_log("TIOCMSET OK status=0x%x", status);
*iptt = *ntx;
return 0;

}

void ptt_close(void)
{
    if (fd >= 0) {
        close(fd);
        fd = -1;
//        ptt_log("ptt_unix: closed fd");
    }
}

