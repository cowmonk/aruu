/* See LICENSE file for copyright and license details. */
/* ?man
env: run command in modified environment
usage: env [-i] [-u var] ... [var=value] ... [cmd [arg ...]]

set environment variables and run a command
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

extern char **environ;

static void
usage(void)
{
	eprintf("usage: %s [-i] [-u var] ... [var=value] ... [cmd [arg ...]]\n", argv0);
}

int
main(int argc, char *argv[])
{
	int savederrno;

	ARGBEGIN {
	// ?man -i: interactive mode or prompt for confirmation
	case 'i':
		*environ = NULL;
		break;
	// ?man -u: unbuffered output
	case 'u':
		if (unsetenv(EARGF(usage())) < 0)
			eprintf("unsetenv:");
		break;
	default:
		usage();
	} ARGEND

	for (; *argv && strchr(*argv, '='); argc--, argv++)
		putenv(*argv);

	if (*argv) {
		execvp(*argv, argv);
		savederrno = errno;
		weprintf("execvp %s:", *argv);
		_exit(126 + (savederrno == ENOENT));
	}

	for (; *environ; environ++)
		puts(*environ);

	return fshut(stdout, "<stdout>");
}
