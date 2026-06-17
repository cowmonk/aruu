/* See LICENSE file for copyright and license details. */


#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"
#include "utf.h"
#include "util.h"

static size_t   startnum = 1;
static size_t   incr = 1;
static size_t   blines = 1;
static size_t   delimlen = 2;
static size_t   seplen = 1;
static int      width = 6;
static int      pflag = 0;
static char     type[] = { 'n', 't', 'n' }; /* footer, body, header */
static char    *delim = "\\:";
static char     format[6] = "%*ld";
static char    *sep = "\t";
static regex_t  preg[3];

static int
getsection(struct line *l, int *section)
{
	size_t i;
	int sectionchanged = 0, newsection = *section;

	for (i = 0; (l->len - i) >= delimlen &&
	     !memcmp(l->data + i, delim, delimlen); i += delimlen) {
		if (!sectionchanged) {
			sectionchanged = 1;
			newsection = 0;
		} else {
			newsection = (newsection + 1) % 3;
		}
	}

	if (!(l->len - i) || l->data[i] == '\n')
		*section = newsection;
	else
		sectionchanged = 0;

	return sectionchanged;
}

static void
nl(const char *fname, FILE *fp)
{
	static struct line line;
	static size_t size;
	size_t number = startnum, bl = 1;
	ssize_t len;
	int donumber, oldsection, section = 1;

	while ((len = getline(&line.data, &size, fp)) > 0) {
		line.len = len;
		donumber = 0;
		oldsection = section;

		if (getsection(&line, &section)) {
			if ((section >= oldsection) && !pflag)
				number = startnum;
			continue;
		}

		switch (type[section]) {
		case 't':
			if (line.data[0] != '\n')
				donumber = 1;
			break;
		/* pexpr line-matching mode */
	case 'p':
			if (!regexec(preg + section, line.data, 0, NULL, 0))
				donumber = 1;
			break;
		case 'a':
			if (line.data[0] == '\n' && bl < blines) {
				++bl;
			} else {
				donumber = 1;
				bl = 1;
			}
		}

		if (donumber) {
			printf(format, width, number);
			fwrite(sep, 1, seplen, stdout);
			number += incr;
		}
		fwrite(line.data, 1, line.len, stdout);
	}
	free(line.data);
	if (ferror(fp))
		eprintf("getline %s:", fname);
}

static void
usage(void)
{
	eprintf("usage: %s [-p] [-b type] [-d delim] [-f type]\n"
	        "       [-h type] [-i num] [-l num] [-n format]\n"
	        "       [-s sep] [-v num] [-w num] [file]\n", argv0);
}

static char
getlinetype(char *type, regex_t *preg)
{
	if (type[0] == 'p')
		eregcomp(preg, type + 1, REG_NOSUB);
	else if (!type[0] || !strchr("ant", type[0]))
		usage();

	return type[0];
}

// ?man nl: line numbering filter
// ?man nl reads lines from file and writes them to stdout, numbering non-empty lines.
// ?man If no file is given nl reads from stdin.
// ?man nl treats the input text as a collection of logical pages divided into
// ?man logical page sections.
// ?man Each logical page consists of a header section, a body
// ?man section and a footer section.
// ?man Sections may be empty.
// ?man The start of each section is indicated by a single delimiting line, one of:
// ?man ::: header
// ?man :: body
// ?man : footer
// ?man If the input text contains no delimiting line then all of the input text
// ?man belongs to a single logical page body section.
int
main(int argc, char *argv[])
{
	FILE *fp = NULL;
	size_t s;
	int ret = 0;
	char *d, *formattype, *formatblit;

	ARGBEGIN {
	// ?man -d:delim: Set delim as the delimiter for logical pages.
	// ?man If delim is only one character, nl appends \":\" to it.
	// ?man The default is \"\\:\".
	case 'd':
		switch (utflen((d = EARGF(usage())))) {
		case 0:
			eprintf("empty logical page delimiter\n");
			break;
		case 1:
			s = strlen(d);
			delim = emalloc(s + 1 + 1);
			estrlcpy(delim, d, s + 1 + 1);
			estrlcat(delim, ":", s + 1 + 1);
			delimlen = s + 1;
			break;
		default:
			delim = d;
			delimlen = strlen(delim);
			break;
		}
		break;
	// ?man -f:type: Define which lines to number in the head | body | footer section:
	// ?man a All lines.
	// ?man n No lines.
	// ?man t Only non-empty lines.
	// ?man This is the default.
	// ?man pexpr Only lines matching expr according to regex 7 or re_format 7 .
	case 'f':
		type[0] = getlinetype(EARGF(usage()), preg);
		break;
	// ?man -b:type: Define which lines to number in the head | body | footer section:
	// ?man a All lines.
	// ?man n No lines.
	// ?man t Only non-empty lines.
	// ?man This is the default.
	// ?man pexpr Only lines matching expr according to regex 7 or re_format 7 .
	case 'b':
		type[1] = getlinetype(EARGF(usage()), preg + 1);
		break;
	// ?man -h:type: Define which lines to number in the head | body | footer section:
	// ?man a All lines.
	// ?man n No lines.
	// ?man t Only non-empty lines.
	// ?man This is the default.
	// ?man pexpr Only lines matching expr according to regex 7 or re_format 7 .
	case 'h':
		type[2] = getlinetype(EARGF(usage()), preg + 2);
		break;
	// ?man -i:num: Set the increment between numbered lines to num .
	case 'i':
		incr = estrtonum(EARGF(usage()), 0, MIN((unsigned long long)LLONG_MAX, (unsigned long long)SIZE_MAX));
		break;
	// ?man -l:num: Set the number of adjacent blank lines to be considered as one to num .
	// ?man The default is 1.
	case 'l':
		blines = estrtonum(EARGF(usage()), 0, MIN((unsigned long long)LLONG_MAX, (unsigned long long)SIZE_MAX));
		break;
	// ?man -n:format: Set the line number output format to one of:
	// ?man ln Left justified.
	// ?man rn Right justified.
	// ?man This is the default.
	// ?man rz Right justified with leading zeroes.
	case 'n':
		formattype = EARGF(usage());
		estrlcpy(format, "%", sizeof(format));

		if (!strcmp(formattype, "ln")) {
			formatblit = "-";
		} else if (!strcmp(formattype, "rn")) {
			formatblit = "";
		} else if (!strcmp(formattype, "rz")) {
			formatblit = "0";
		} else {
			eprintf("%s: bad format\n", formattype);
		}

		estrlcat(format, formatblit, sizeof(format));
		estrlcat(format, "*ld", sizeof(format));
		break;
	// ?man -p: Do not reset line number for logical pages.
	case 'p':
		pflag = 1;
		break;
	// ?man -s:sep: Use sep to separate line numbers and lines.
	// ?man The default is \"\\t\".
	case 's':
		sep = EARGF(usage());
		seplen = unescape(sep);
		break;
	// ?man -v:num: Start counting lines from num .
	// ?man The default is 1.
	case 'v':
		startnum = estrtonum(EARGF(usage()), 0, MIN((unsigned long long)LLONG_MAX, (unsigned long long)SIZE_MAX));
		break;
	// ?man -w:num: Set the width of the line number to num .
	// ?man The default is 6.
	case 'w':
		width = estrtonum(EARGF(usage()), 1, INT_MAX);
		break;
	default:
		usage();
	} ARGEND

	if (argc > 1)
		usage();

	if (!argc) {
		nl("<stdin>", stdin);
	} else {
		if (!strcmp(argv[0], "-")) {
			argv[0] = "<stdin>";
			fp = stdin;
		} else if (!(fp = fopen(argv[0], "r"))) {
			eprintf("fopen %s:", argv[0]);
		}
		nl(argv[0], fp);
	}

	ret |= fp && fp != stdin && fshut(fp, argv[0]);
	ret |= fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>");

	return ret;
}
