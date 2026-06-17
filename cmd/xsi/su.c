/* See LICENSE file for copyright and license details. */


#include <sys/types.h>

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "passwd.h"
#include "util.h"

extern char **environ;

static int lflag = 0;
static int pflag = 0;

static void
usage(void)
{
	eprintf("usage: %s [-lp] [username]\n", argv0);
}

// ?man su: run a command with a substitute user and group ID
// ?man arguments: username
// ?man su allows to run commands with a substitute user and group ID.
// ?man When called without arguments, su defaults to running an interactive shell as root.
// ?man For backward compatibility su defaults to not change the current directory and to only set the environment variables HOME and SHELL plus USER and LOGNAME if the target username is not root.
int
main(int argc, char *argv[])
{
	char *usr, *pass;
	char *shell, *envshell, *term;
	struct passwd *pw;
	char *newargv[3];
	uid_t uid;

	ARGBEGIN {
	// ?man -l: Starts the shell as login shell with an environment similar to a real login.
	case 'l':
		lflag = 1;
		break;
	// ?man -p: Preserves the whole environment. This option is ignored if the -l option is specified.
	case 'p':
		pflag = 1;
		break;
	default:
		usage();
	} ARGEND;

	if (argc > 1)
		usage();
	usr = argc > 0 ? argv[0] : "root";

	errno = 0;
	pw = getpwnam(usr);
	if (!pw) {
		if (errno)
			eprintf("getpwnam: %s:", usr);
		else
			eprintf("who are you?\n");
	}

	uid = getuid();
	if (uid) {
		pass = getpass("Password: ");
		if (!pass)
			eprintf("getpass:");
		if (pw_check(pw, pass) <= 0)
			exit(1);
	}

	if (initgroups(usr, pw->pw_gid) < 0)
		eprintf("initgroups:");
	if (setgid(pw->pw_gid) < 0)
		eprintf("setgid:");
	if (setuid(pw->pw_uid) < 0)
		eprintf("setuid:");

	shell = pw->pw_shell[0] == '\0' ? "/bin/sh" : pw->pw_shell;
	if (lflag) {
		term = getenv("TERM");
		clearenv();
		setenv("HOME", pw->pw_dir, 1);
		setenv("SHELL", shell, 1);
		setenv("USER", pw->pw_name, 1);
		setenv("LOGNAME", pw->pw_name, 1);
		setenv("TERM", term ? term : "linux", 1);
		if (chdir(pw->pw_dir) < 0)
			eprintf("chdir %s:", pw->pw_dir);
		newargv[0] = shell;
		newargv[1] = "-l";
		newargv[2] = NULL;
	} else {
		if (pflag) {
			envshell = getenv("SHELL");
			if (envshell && envshell[0] != '\0')
				shell = envshell;
		} else {
			setenv("HOME", pw->pw_dir, 1);
			setenv("SHELL", shell, 1);
			if (strcmp(pw->pw_name, "root") != 0) {
				setenv("USER", pw->pw_name, 1);
				setenv("LOGNAME", pw->pw_name, 1);
			}
		}
		newargv[0] = shell;
		newargv[1] = NULL;
	}
	execve(shell, newargv, environ);
	weprintf("execve %s:", shell);
	return (errno == ENOENT) ? 127 : 126;
}
