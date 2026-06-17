/* See LICENSE file for copyright and license details. */


#include "queue.h"
#include "util.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct type {
	unsigned char     format;
	unsigned int      len;
	TAILQ_ENTRY(type) entry;
};

static TAILQ_HEAD(head, type) head = TAILQ_HEAD_INITIALIZER(head);
static unsigned char addr_format = 'o';
static off_t skip = 0;
static off_t max = -1;
static size_t linelen = 1;
static int big_endian;

static void
printaddress(off_t addr)
{
	char fmt[] = "%07j#";

	if (addr_format == 'n') {
		fputc(' ', stdout);
	} else {
		fmt[4] = addr_format;
		printf(fmt, (intmax_t)addr);
	}
}

static void
printchunk(const unsigned char *s, unsigned char format, size_t len)
{
	long long res, basefac;
	size_t i;
	char fmt[] = " %#*ll#";
	unsigned char c;

	const char *namedict[] = {
		"nul", "soh", "stx", "etx", "eot", "enq", "ack",
		"bel", "bs",  "ht",  "nl",  "vt",  "ff",  "cr",
		"so",  "si",  "dle", "dc1", "dc2", "dc3", "dc4",
		"nak", "syn", "etb", "can", "em",  "sub", "esc",
		"fs",  "gs",  "rs",  "us",  "sp",
	};
	const char *escdict[] = {
		['\0'] = "\\0", ['\a'] = "\\a",
		['\b'] = "\\b", ['\t'] = "\\t",
		['\n'] = "\\n", ['\v'] = "\\v",
		['\f'] = "\\f", ['\r'] = "\\r",
	};

	switch (format) {
	// equivalent to -t a
	case 'a':
		c = *s & ~128; /* clear high bit as required by standard */
		if (c < LEN(namedict) || c == 127) {
			printf(" %3s", (c == 127) ? "del" : namedict[c]);
		} else {
			printf(" %3c", c);
		}
		break;
	// equivalent to -t c
	case 'c':
		if (strchr("\a\b\t\n\v\f\r\0", *s)) {
			printf(" %3s", escdict[*s]);
		} else if (!isprint(*s)) {
			printf(" %3o", *s);
		} else {
			printf(" %3c", *s);
		}
		break;
	default:
		if (big_endian) {
			for (res = 0, basefac = 1, i = len; i; i--) {
				res += s[i - 1] * basefac;
				basefac <<= 8;
			}
		} else {
			for (res = 0, basefac = 1, i = 0; i < len; i++) {
				res += s[i] * basefac;
				basefac <<= 8;
			}
		}
		fmt[2] = big_endian ? '-' : ' ';
		fmt[6] = format;
		printf(fmt, (int)(3 * len + len - 1), res);
	}
}

static void
printline(const unsigned char *line, size_t len, off_t addr)
{
	struct type *t = NULL;
	size_t i;
	int first = 1;
	unsigned char *tmp;

	if (TAILQ_EMPTY(&head))
		goto once;
	TAILQ_FOREACH(t, &head, entry) {
once:
		if (first) {
			printaddress(addr);
			first = 0;
		} else {
			printf("%*c", (addr_format == 'n') ? 1 : 7, ' ');
		}
		for (i = 0; i < len; i += MIN(len - i, t ? t->len : 4)) {
			if (len - i < (t ? t->len : 4)) {
				tmp = ecalloc(t ? t->len : 4, 1);
				memcpy(tmp, line + i, len - i);
				printchunk(tmp, t ? t->format : 'o',
				           t ? t->len : 4);
				free(tmp);
			} else {
				printchunk(line + i, t ? t->format : 'o',
				           t ? t->len : 4);
			}
		}
		fputc('\n', stdout);
		if (TAILQ_EMPTY(&head) || (!len && !first))
			break;
	}
}

static int
od(int fd, char *fname, int last)
{
	static unsigned char *line;
	static size_t lineoff;
	static off_t addr;
	unsigned char buf[BUFSIZ];
	size_t i, size = sizeof(buf);
	ssize_t n;

	while (skip - addr > 0) {
		n = read(fd, buf, MIN((size_t)(skip - addr), sizeof(buf)));
		if (n < 0)
			weprintf("read %s:", fname);
		if (n <= 0)
			return n;
		addr += n;
	}
	if (!line)
		line = emalloc(linelen);

	for (;;) {
		if (max >= 0)
			size = MIN((size_t)(max - (addr - skip)), size);
		if ((n = read(fd, buf, size)) <= 0)
			break;
		for (i = 0; i < (size_t)n; i++, addr++) {
			line[lineoff++] = buf[i];
			if (lineoff == linelen) {
				printline(line, lineoff, addr - lineoff + 1);
				lineoff = 0;
			}
		}
	}
	if (n < 0) {
		weprintf("read %s:", fname);
		return n;
	}
	if (lineoff && last)
		printline(line, lineoff, addr - lineoff);
	if (last)
		printline((unsigned char *)"", 0, addr);
	return 0;
}

static int
lcm(unsigned int a, unsigned int b)
{
	unsigned int c, d, e;

	for (c = a, d = b; c ;) {
		e = c;
		c = d % c;
		d = e;
	}

	return a / d * b;
}

static void
addtype(char format, int len)
{
	struct type *t;

	t = emalloc(sizeof(*t));
	t->format = format;
	t->len = len;
	TAILQ_INSERT_TAIL(&head, t, entry);
}

static void
usage(void)
{
	eprintf("usage: %s [-bdosvx] [-A addressformat] "
#if FEATURE_OD_ENDIAN
	        "[-E | -e] "
#endif
	        "[-j skip] [-t outputformat] [file ...]\n", argv0);
}

// ?man od: octal dump
// ?man od writes an octal dump of each file to stdout.
// ?man If no file is given od reads from stdin.
int
main(int argc, char *argv[])
{
	struct type *t;
	char *s, *end;
	int fd, ret = 0, len, fmt_char;

	big_endian = (*(uint16_t *)"\0\xff" == 0xff);

	ARGBEGIN {
	// ?man -A:addressformat: addressformat is one of d|o|x|n and sets the address to be
	// ?man either in decimal, octal, hexadecimal or not printed at all.
	// ?man The default is octal.
	case 'A':
		s = EARGF(usage());
		if (strlen(s) != 1 || !strchr("doxn", s[0]))
			usage();
		addr_format = s[0];
		break;
	// ?man -b: Equivalent to -t o1 .
	case 'b':
		addtype('o', 1);
		break;
	// ?man -d: Equivalent to -t u2 .
	case 'd':
		addtype('u', 2);
		break;
#if FEATURE_OD_ENDIAN
	// ?man -E: Force Little Endian ( e ) or Big Endian ( E ) system-independently.
	case 'E':
	// ?man -e: Force Little Endian ( e ) or Big Endian ( E ) system-independently.
	case 'e':
		big_endian = (ARGC() == 'E');
		break;
#endif
	// ?man -j:skip: Ignore the first skip bytes of input.
	case 'j':
		if ((skip = parseoffset(EARGF(usage()))) < 0)
			usage();
		break;
	// ?man -N:num: read at most num bytes of input
	case 'N':
		if ((max = parseoffset(EARGF(usage()))) < 0)
			usage();
		break;
	// ?man -o: Equivalent to -t o2 .
	case 'o':
		addtype('o', 2);
		break;
	// ?man -s: Equivalent to -t d2 .
	case 's':
		addtype('d', 2);
		break;
	// ?man -t:outputformat: outputformat is a list of a|c|d|o|u|x followed by a digit or C|S|I|L and sets
	// ?man the content to be in named character, character, signed
	// ?man decimal, octal, unsigned decimal, or
	// ?man hexadecimal format, processing the given amount of bytes or the length
	// ?man of Char, Short, Integer or Long.
	// ?man The default is octal with 4 bytes.
	case 't':
		s = EARGF(usage());
		for (; *s; s++) {
			switch (*s) {
	/* outputformat a is equivalent to -t a */
	case 'a':
	/* outputformat c is equivalent to -t c */
	case 'c':
				addtype(*s, 1);
				break;
	/* outputformat d is equivalent to -t d */
	case 'd':
	/* outputformat o is equivalent to -t o */
	case 'o':
	/* outputformat u is equivalent to -t u */
	case 'u':
	/* outputformat x is equivalent to -t x */
	case 'x':
				fmt_char = *s;
				if (isdigit((unsigned char)*(s + 1))) {
					len = strtol(s + 1, &end, 10);
					s = end - 1;
				} else {
					switch (*(s + 1)) {
	/* outputformat C uses sizeof(char) bytes */
	case 'C':
						len = sizeof(char);
						s++;
						break;
	/* outputformat S uses sizeof(short) bytes */
	case 'S':
						len = sizeof(short);
						s++;
						break;
	/* outputformat I uses sizeof(int) bytes */
	case 'I':
						len = sizeof(int);
						s++;
						break;
	/* outputformat L uses sizeof(long) bytes */
	case 'L':
						len = sizeof(long);
						s++;
						break;
					default:
						len = sizeof(int);
					}
				}
				addtype(fmt_char, len);
				break;
			default:
				usage();
			}
		}
		break;
	// ?man -v: Always set.
	// ?man Write all input data, including duplicate lines.
	case 'v':
		/* always set, use uniq(1) to handle duplicate lines */
		break;
	// ?man -x: Equivalent to -t x2 .
	case 'x':
		addtype('x', 2);
		break;
	default:
		usage();
	} ARGEND

	/* line length is lcm of type lengths and >= 16 by doubling */
	TAILQ_FOREACH(t, &head, entry)
		linelen = lcm(linelen, t->len);
	if (TAILQ_EMPTY(&head))
		linelen = 16;
	while (linelen < 16)
		linelen *= 2;

	if (!argc) {
		if (od(0, "<stdin>", 1) < 0)
			ret = 1;
	} else {
		for (; *argv; argc--, argv++) {
			if (!strcmp(*argv, "-")) {
				*argv = "<stdin>";
				fd = 0;
			} else if ((fd = open(*argv, O_RDONLY)) < 0) {
				weprintf("open %s:", *argv);
				ret = 1;
				continue;
			}
			if (od(fd, *argv, (!*(argv + 1))) < 0)
				ret = 1;
			if (fd != 0)
				close(fd);
		}
	}

	ret |= fshut(stdout, "<stdout>") | fshut(stderr, "<stderr>");

	return ret;
}
