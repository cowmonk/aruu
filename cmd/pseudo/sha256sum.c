/* See LICENSE file for copyright and license details. */


#include <stdint.h>
#include <stdio.h>

#include "crypt.h"
#include "sha256.h"
#include "util.h"

static struct sha256 s;
struct crypt_ops sha256_ops = {
	sha256_init,
	sha256_update,
	sha256_sum,
	&s,
};

static void
usage(void)
{
	eprintf("usage: %s [-c] [file ...]\n", argv0);
}

// ?man sha256sum: compute sha256 checksums
// ?man arguments: file ...
// ?man compute and check sha256 message digests
int
main(int argc, char *argv[])
{
	int ret = 0, (*cryptfunc)(int, char **, struct crypt_ops *, uint8_t *, size_t) = cryptmain;
	uint8_t md[SHA256_DIGEST_LENGTH];

	ARGBEGIN {
	// ?man -b: accepted for compatibility; ignored
	case 'b':
	// ?man -t: accepted for compatibility; ignored
	case 't':
		/* ignore */
		break;
	// ?man -c: read checksums from files and verify them
	case 'c':
		cryptfunc = cryptcheck;
		break;
	default:
		usage();
	} ARGEND

	ret |= cryptfunc(argc, argv, &sha256_ops, md, sizeof(md));
	ret |= fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>");

	return ret;
}
