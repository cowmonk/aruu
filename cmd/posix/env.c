/* See LICENSE file for copyright and license details. */


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

// ?man env: run command in modified environment
// ?man arguments: [name=value ...] [cmd [arg ...]]
// ?man set environment variables and run a command
int
main(int argc, char *argv[])
{
	int savederrno;

	ARGBEGIN {
	// ?man -i: ignore the inherited environment and start from an empty one
	case 'i':
		*environ = NULL;
		break;
	// ?man -u:name: remove name from the environment
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
