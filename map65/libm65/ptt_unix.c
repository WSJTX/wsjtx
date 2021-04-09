/*
 * WSJT is Copyright (c) 2001-2006 by Joseph H. Taylor, Jr., K1JT, 
 * and is licensed under the GNU General Public License (GPL).
 *
 * Code used from cwdaemon for parallel port ptt only.
 *
 * cwdaemon - morse sounding daemon for the parallel or serial port
 * Copyright (C) 2002 -2005 Joop Stakenborg <pg4i@amsat.org>
 *                       and many authors, see the AUTHORS file.
 *
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
# if HAVE_STDIO_H
# include <stdio.h>
#endif
#if STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# if HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#if HAVE_UNISTD_H
# include <unistd.h>
#endif
#if HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#if HAVE_FCNTL_H
# include <fcntl.h>
#endif

#ifdef HAVE_LINUX_PPDEV_H
# include <linux/ppdev.h>
# include <linux/parport.h>
#endif
#ifdef HAVE_DEV_PPBUS_PPI_H
# include <dev/ppbus/ppi.h>
# include <dev/ppbus/ppbconf.h>
#endif

int lp_reset (int fd);
int lp_ptt (int fd, int onoff);

#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#if (defined(__unix__) || defined(unix)) && !defined(USG)
# include <sys/param.h>
#endif

#include <string.h>
/* parport functions */

int dev_is_parport(int fd);
int ptt_parallel(int fd, int *ntx, int *iptt);
int ptt_serial(int fd, int *ntx, int *iptt);

int fd=-1;		/* Used for both serial and parallel */

/*
 * ptt_
 *
 * generic unix PTT routine called from Fortran
 *
 * Inputs	
 * unused	Unused, to satisfy old windows calling convention
 * ptt_port	device name serial or parallel
 * ntx		pointer to fortran command on or off
 * iptt		pointer to fortran command status on or off
 * Returns	- non 0 if error
*/

/* Tiny state machine */
#define STATE_PORT_CLOSED		0
#define STATE_PORT_OPEN_PARALLEL	1
#define STATE_PORT_OPEN_SERIAL		2

//int ptt_(int *unused, char *ptt_port, int *ntx, int *iptt)
int ptt_(int *unused, int *ntx, int *iptt)
{
  static int state=0;
  char *p;

// ### Temporary:
  char* ptt_port;
  if(*unused != -99) {
    *iptt=*ntx;
    return 0;
  }
// ###

  /* In the very unlikely event of a NULL pointer, just return.
   * Yes, I realise this should not be possible in WSJT.
   */
  if (ptt_port == NULL) {
    *iptt = *ntx;
    return (0);
  }
    
  switch (state) {
  case STATE_PORT_CLOSED:

     /* Remove trailing ' ' */
    if ((p = strchr(ptt_port, ' ')) != NULL)
      *p = '\0';

    /* If all that is left is a '\0' then also just return */
    if (*ptt_port == '\0') {
      *iptt = *ntx;
      return(0);
    }

    if ((fd = open(ptt_port, O_RDWR|O_NONBLOCK)) < 0) {
	fprintf(stderr, "Can't open %s.\n", ptt_port);
	return (1);
    }

    if (dev_is_parport(fd)) {
      state = STATE_PORT_OPEN_PARALLEL;
      lp_reset(fd);
      ptt_parallel(fd, ntx, iptt);
    } else {
      state = STATE_PORT_OPEN_SERIAL;
      ptt_serial(fd, ntx, iptt);
    }
    break;

  case STATE_PORT_OPEN_PARALLEL:
    ptt_parallel(fd, ntx, iptt);
    break;

  case STATE_PORT_OPEN_SERIAL:
    ptt_serial(fd, ntx, iptt);
    break;

  default:
    close(fd);
    fd = -1;
    state = STATE_PORT_CLOSED;
    break;
  }
  return(0);
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
  int control = TIOCM_RTS | TIOCM_DTR;

  if(*ntx) {
    ioctl(fd, TIOCMBIS, &control);               /* Set DTR and RTS */
    *iptt = 1;
  } else {
    ioctl(fd, TIOCMBIC, &control);
    *iptt = 0;
  }
  return(0);
}


/* parport functions */

/*
 * dev_is_parport(fd):
 *
 * inputs	- Already open fd
 * output	- 1 if parallel port, 0 if not
 * side effects	- Unfortunately, this is platform specific.
 */

#if defined(HAVE_LINUX_PPDEV_H)                /* Linux (ppdev) */

int
dev_is_parport(int fd)
{
       struct stat st;
       int m;

       if ((fstat(fd, &st) == -1) ||
	   ((st.st_mode & S_IFMT) != S_IFCHR) ||
	   (ioctl(fd, PPGETMODE, &m) == -1))
	 return(0);

       return(1);
}

#elif defined(HAVE_DEV_PPBUS_PPI_H)    /* FreeBSD (ppbus/ppi) */

int
dev_is_parport(int fd)
{
       struct stat st;
       unsigned char c;

       if ((fstat(fd, &st) == -1) ||
	   ((st.st_mode & S_IFMT) != S_IFCHR) ||
	   (ioctl(fd, PPISSTATUS, &c) == -1))
	 return(0);

       return(1);
}

#else                                  /* Fallback (nothing) */

int
dev_is_parport(int fd)
{
       return(0);
}

#endif
/* Linux wrapper around PPFCONTROL */
#ifdef HAVE_LINUX_PPDEV_H
static void
parport_control (int fd, unsigned char controlbits, int values)
{
	struct ppdev_frob_struct frob;
	frob.mask = controlbits;
	frob.val = values;

	if (ioctl (fd, PPFCONTROL, &frob) == -1)
	{
		fprintf(stderr, "Parallel port PPFCONTROL");
		exit (1);
	}
}
#endif

/* FreeBSD wrapper around PPISCTRL */
#ifdef HAVE_DEV_PPBUS_PPI_H
static void
parport_control (int fd, unsigned char controlbits, int values)
{
	unsigned char val;

	if (ioctl (fd, PPIGCTRL, &val) == -1)
	{
		fprintf(stderr, "Parallel port PPIGCTRL");
		exit (1);
	}

	val &= ~controlbits;
	val |= values;

	if (ioctl (fd, PPISCTRL, &val) == -1)
	{
		fprintf(stderr, "Parallel port PPISCTRL");
		exit (1);
	}
}
#endif

/* Initialise a parallel port, given open fd */
int
lp_init (int fd)
{
#ifdef HAVE_LINUX_PPDEV_H
	int mode;
#endif

#ifdef HAVE_LINUX_PPDEV_H
	mode = PARPORT_MODE_PCSPP;

	if (ioctl (fd, PPSETMODE, &mode) == -1)
	{
		fprintf(stderr, "Setting parallel port mode");
		close (fd);
		return(-1);
	}

	if (ioctl (fd, PPEXCL, NULL) == -1)
	{
		fprintf(stderr, "Parallel port is already in use.\n");
		close (fd);
		return(-1);
	}
	if (ioctl (fd, PPCLAIM, NULL) == -1)
	{
		fprintf(stderr, "Claiming parallel port.\n");
		fprintf(stderr, "HINT: did you unload the lp kernel module?");
		close (fd);
		return(-1);
	}

	/* Enable CW & PTT - /STROBE bit (pin 1) */
	parport_control (fd, PARPORT_CONTROL_STROBE, PARPORT_CONTROL_STROBE);
#endif
#ifdef HAVE_DEV_PPBUS_PPI_H
	parport_control (fd, STROBE, STROBE);
#endif
	lp_reset (fd);
	return(0);
}

/* release ppdev and close port */
int
lp_free (int fd)
{
#ifdef HAVE_LINUX_PPDEV_H
	lp_reset (fd);

	/* Disable CW & PTT - /STROBE bit (pin 1) */
	parport_control (fd, PARPORT_CONTROL_STROBE, 0);

	ioctl (fd, PPRELEASE);
#endif
#ifdef HAVE_DEV_PPBUS_PPI_H
	/* Disable CW & PTT - /STROBE bit (pin 1) */
	parport_control (fd, STROBE, 0);
#endif
	close (fd);
	return(0);
}

/* set to a known state */
int
lp_reset (int fd)
{
#if defined (HAVE_LINUX_PPDEV_H) || defined (HAVE_DEV_PPBUS_PPI_H)
	lp_ptt (fd, 0);
#endif
	return(0);
}

/* SSB PTT keying - /INIT bit (pin 16) (inverted) */
int
lp_ptt (int fd, int onoff)
{
#ifdef HAVE_LINUX_PPDEV_H
	if (onoff == 1)
		parport_control (fd, PARPORT_CONTROL_INIT,
				PARPORT_CONTROL_INIT);
	else
		parport_control (fd, PARPORT_CONTROL_INIT, 0);
#endif
#ifdef HAVE_DEV_PPBUS_PPI_H
	if (onoff == 1)
		parport_control (fd, nINIT,
				nINIT);
	else
		parport_control (fd, nINIT, 0);
#endif
	return(0);
}

/*
 * ptt_parallel
 *
 * generic parallel unix PTT routine called indirectly from Fortran
 *
 * fd		- already opened file descriptor
 * ntx		- pointer to fortran command on or off
 * iptt		- pointer to fortran command status on or off
 */

int
ptt_parallel(int fd, int *ntx, int *iptt)
{
  if(*ntx) {
    lp_ptt(fd, 1);
    *iptt=1;
  }  else {
    lp_ptt(fd, 0);
    *iptt=0;
  }
  return(0);
}
