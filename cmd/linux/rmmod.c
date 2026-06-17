
#include <sys/syscall.h>

#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static void
usage(void)
{
	eprintf("usage: %s [-fw] module...\n", argv0);
}

// ?man rmmod: remove a module from the Linux kernel
// ?man arguments: module...
// ?man rmmod removes one or more modules from the kernel.
int
main(int argc, char *argv[])
{
	char *mod, *p;
	int i;
	int flags = O_NONBLOCK;

	ARGBEGIN {
	// ?man -f: This option can be extremely dangerous: it has no effect unless CONFIG_MODULE_FORCE_UNLOAD was set when the kernel was compiled. With this option, you can remove modules which are being used, or which are not designed to be removed, or have been marked as unsafe.
	case 'f':
		flags |= O_TRUNC;
		break;
	// ?man -w: Normally, rmmod will refuse to unload modules which are in use. With this option, rmmod will isolate the module, and wait until the module is no longer used. Noone new will be able to use the module, but it's up to you to make sure the current users eventually finish with it.
	case 'w':
		flags &= ~O_NONBLOCK;
		break;
	default:
		usage();
	} ARGEND;

	if (argc < 1)
		usage();

	for (i = 0; i < argc; i++) {
		mod = argv[i];
		p = strrchr(mod, '.');
		if (p && !strcmp(p, ".ko"))
			*p = '\0';
		if (syscall(__NR_delete_module, mod, flags) < 0)
			eprintf("delete_module:");
	}

	return 0;
}
