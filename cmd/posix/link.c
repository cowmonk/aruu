/* See LICENSE file for copyright and license details. */
/* ?man
link: create a link to a file
usage: link target name

create a hard link to an existing file
*/

#include <unistd.h>

#include "util.h"

static void
usage(void)
{
	eprintf("usage: %s target name\n", argv0);
}

int
main(int argc, char *argv[])
{
	ARGBEGIN {
	default:
		usage();
	} ARGEND

	if (argc != 2)
		usage();

	if (link(argv[0], argv[1]) < 0)
		eprintf("link:");

	return 0;
}
