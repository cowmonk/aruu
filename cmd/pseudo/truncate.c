/* See LICENSE file for copyright and license details. */


#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "util.h"

static void
usage(void)
{
	eprintf("usage: %s [-c] -s size file...\n", argv0);
}

// ?man truncate: shrink or extend the size of a file to the specified size
// ?man synopsis: [-c] -s size file ...
// ?man truncate shrinks or extends the size of each file specified size. A file argument that does not exist is created. If a file is larger than the specified size, the extra data is lost. If a file is shorter, it is extended and the extended part reads as zero bytes.
int
main(int argc, char *argv[])
{
	int cflag = 0, sflag = 0;
	int fd, i, ret = 0;
	long size = 0;

	ARGBEGIN {
	// ?man -s:size: Set or adjust the file size by size bytes.
	case 's':
		sflag = 1;
		size = estrtol(EARGF(usage()), 10);
		break;
	// ?man -c: Do not create any files.
	case 'c':
		cflag = 1;
		break;
	default:
		usage();
	} ARGEND;

	if (argc < 1 || sflag == 0)
		usage();

	for (i = 0; i < argc; i++) {
		fd = open(argv[i], O_WRONLY | (cflag ? 0 : O_CREAT), 0644);
		if (fd < 0) {
			weprintf("open: cannot open `%s' for writing:", argv[i]);
			ret = 1;
			continue;
		}
		if (ftruncate(fd, size) < 0) {
			weprintf("ftruncate: cannot open `%s' for writing:", argv[i]);
			ret = 1;
		}
		close(fd);
	}
	return ret;
}
