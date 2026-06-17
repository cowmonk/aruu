/* See LICENSE file for copyright and license details. */


#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static void
usage(void)
{
	eprintf("usage: %s [-muinpU] cmd [args...]\n", argv0);
}

// ?man unshare: run program with some namespaces unshared from parent
// ?man arguments: cmd [args ...]
// ?man unshare unshares the indicated namespaces from the parent process and then executes the specified program.
// ?man The namespaces to be unshared are indicated via options.
int
main(int argc, char *argv[])
{
	int flags = 0;

	ARGBEGIN {
	// ?man -m: Unshare the mount namespace, so that the calling process has a private copy of its namespace which is not shared with any other process. This flag has the same effect as the clone CLONE_NEWNS flag.
	case 'm':
		flags |= CLONE_NEWNS;
		break;
	// ?man -u: Unshare the UTS IPC namespace, so that the calling process has a private copy of the UTS namespace which is not shared with any other process. This flag has the same effect as the clone CLONE_NEWUTS flag.
	case 'u':
		flags |= CLONE_NEWUTS;
		break;
	// ?man -i: Unshare the System V IPC namespace, so that the calling process has a private copy of the System V IPC namespace which is not shared with any other process. This flag has the same effect as the clone CLONE_NEWIPC flag.
	case 'i':
		flags |= CLONE_NEWIPC;
		break;
	// ?man -n: Unshare the network namespace, so that the calling process is moved into a new network namespace which is not shared with any previously existing process. This flag has the same effect as the clone CLONE_NEWNET flag.
	case 'n':
		flags |= CLONE_NEWNET;
		break;
	// ?man -p: Create the process in a new PID namespace. This flag has the same effect as the clone CLONE_NEWPID flag.
	case 'p':
		flags |= CLONE_NEWPID;
		break;
	// ?man -U: The process will have a distinct set of UIDs, GIDs and capabilities.
	case 'U':
		flags |= CLONE_NEWUSER;
		break;
	default:
		usage();
	} ARGEND;

	if (argc < 1)
		usage();

	if (unshare(flags) < 0)
		eprintf("unshare:");

	if (execvp(argv[0], argv) < 0)
		eprintf("execvp:");

	return 0;
}
