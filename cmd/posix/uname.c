/* See LICENSE file for copyright and license details. */


#include <sys/utsname.h>

#include <stdio.h>

#include "util.h"

static void
usage(void)
{
	eprintf("usage: %s [-amnrsv]\n", argv0);
}

// ?man uname: print system info
// ?man display system hostname, kernel name, release, and architecture
int
main(int argc, char *argv[])
{
	struct utsname u;
	int mflag = 0, nflag = 0, rflag = 0, sflag = 0, vflag = 0;

	ARGBEGIN {
	// ?man -a: print all available system information
	case 'a':
		mflag = nflag = rflag = sflag = vflag = 1;
		break;
	// ?man -m: print the machine hardware name
	case 'm':
		mflag = 1;
		break;
	// ?man -n: print the network node hostname
	case 'n':
		nflag = 1;
		break;
	// ?man -r: print the operating system release
	case 'r':
		rflag = 1;
		break;
	// ?man -s: print the operating system name
	case 's':
		sflag = 1;
		break;
	// ?man -v: print the operating system version
	case 'v':
		vflag = 1;
		break;
	default:
		usage();
	} ARGEND

	if (argc)
		usage();

	if (uname(&u) < 0)
		eprintf("uname:");

	if (sflag || !(nflag || rflag || vflag || mflag))
		putword(stdout, u.sysname);
	if (nflag)
		putword(stdout, u.nodename);
	if (rflag)
		putword(stdout, u.release);
	if (vflag)
		putword(stdout, u.version);
	if (mflag)
		putword(stdout, u.machine);
	putchar('\n');

	return fshut(stdout, "<stdout>");
}
