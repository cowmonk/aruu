/* See LICENSE file for copyright and license details. */
/* ?man
dirname: strip non-directory suffix from filename
usage: dirname path

print the directory portion of a pathname
*/

#include <libgen.h>
#include <stdio.h>

#include "util.h"

static void
usage(void)
{
	eprintf("usage: %s path\n", argv0);
}

int
main(int argc, char *argv[])
{
	ARGBEGIN {
	default:
		usage();
	} ARGEND

	if (argc != 1)
		usage();

	puts(dirname(argv[0]));

	return fshut(stdout, "<stdout>");
}
