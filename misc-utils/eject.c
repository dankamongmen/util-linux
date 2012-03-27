/*
 * Copyright (C) 1994-2005 Jeff Tranter (tranter@pobox.com)
 * Copyright (C) 2012 Karel Zak <kzak@redhat.com>
 * Copyright (C) Michal Luscon <mluscon@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <err.h>
#include <stdarg.h>

#include <getopt.h>
#include <errno.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/mtio.h>
#include <linux/cdrom.h>
#include <linux/fd.h>
#include <sys/mount.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <scsi/scsi_ioctl.h>
#include <sys/time.h>

#include "linux_version.h"
#include "c.h"
#include "nls.h"
#include "strutils.h"
#include "xalloc.h"
#include "canonicalize.h"

#define EJECT_DEFAULT_DEVICE "/dev/cdrom"


/* Used by the toggle_tray() function. If ejecting the tray takes this
 * time or less, the tray was probably already ejected, so we close it
 * again.
 */
#define TRAY_WAS_ALREADY_OPEN_USECS  200000	/* about 0.2 seconds */


/* Global Variables */
static int a_option; /* command flags and arguments */
static int c_option;
static int d_option;
static int f_option;
static int n_option;
static int q_option;
static int r_option;
static int s_option;
static int t_option;
static int T_option;
static int v_option;
static int x_option;
static int p_option;
static int m_option;
static int a_arg;
static long int c_arg;
static long int x_arg;

/*
 * These are the basenames of devices which can have multiple
 * partitions per device.
 */
const char *partition_device[] = {
    "hd",
    "sd",
    "xd",
    "dos_hd",
    "mfm",
    "ad",
    "ed",
    "ftl",
    "pd",
    0
};

static void __attribute__ ((__noreturn__)) usage(FILE * out)
{
	fputs(USAGE_HEADER, out);

	fprintf(out,
		_(" %s [options] [<device>|<mountpoint>]\n"), program_invocation_short_name);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -h, --help         display command usage and exit\n"
	        " -V  --version      display program version and exit\n"
		" -d, --default      display default device\n"
		" -a, --auto         turn auto-eject feature on or off\n"
		" -c, --changerslot  switch discs on a CD-ROM changer\n"
		" -t, --trayclose    close tray\n"
		" -T, --traytoggle   toggle tray\n"
		" -x, --cdspeed      set CD-ROM max speed\n"
		" -v, --verbose      enable verbose output\n"
		" -n, --noop         don't eject, just show device found\n"
		" -r, --cdrom        eject CD-ROM\n"
		" -s, --scsi         eject SCSI device\n"
		" -f, --loppy        eject floppy\n"
		" -q, --tape         eject tape\n"
		" -p, --proc         use /proc/mounts instead of /etc/mtab\n"
		" -m, --no-unmount   do not unmount device even if it is mounted\n"),
		out);

	fputs(_("\nBy default tries -r, -s, -f, and -q in order until success.\n"), out);
	fprintf(out, USAGE_MAN_TAIL("eject(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}


/* Handle command line options. */
static void parse_args(int argc, char **argv, char **device)
{
	static struct option long_opts[] =
	{
		{"help",	no_argument,	   NULL, 'h'},
		{"verbose",	no_argument,	   NULL, 'v'},
		{"default",	no_argument,	   NULL, 'd'},
		{"auto",	required_argument, NULL, 'a'},
		{"changerslot", required_argument, NULL, 'c'},
		{"trayclose",	no_argument,	   NULL, 't'},
		{"traytoggle",	no_argument,	   NULL, 'T'},
		{"cdspeed",	required_argument, NULL, 'x'},
		{"noop",	no_argument,	   NULL, 'n'},
		{"cdrom",	no_argument,	   NULL, 'r'},
		{"scsi",	no_argument,	   NULL, 's'},
		{"floppy",	no_argument,	   NULL, 'f'},
		{"tape",	no_argument,	   NULL, 'q'},
		{"version",	no_argument,	   NULL, 'V'},
		{"proc",	no_argument,	   NULL, 'p'},
		{"no-unmount",	no_argument,	   NULL, 'm'},
		{0, 0, 0, 0}
	};
	int c;

	while ((c = getopt_long(argc, argv,
				"a:c:x:dfhnqrstTvVpm", long_opts, NULL)) != -1) {
		switch (c) {
		case 'a':
			a_option = 1;
			if (!strcmp(optarg, "0") || !strcmp(optarg, "off"))
				a_arg = 0;
			else if (!strcmp(optarg, "1") || !strcmp(optarg, "on"))
				a_arg = 1;
			else
				errx(EXIT_FAILURE, _("invalid argument to --auto/-a option"));
			break;
		case 'c':
			c_option = 1;
			c_arg = strtoul_or_err(optarg, _("invalid argument to --changerslot/-c option"));
			break;
		case 'x':
			x_option = 1;
			x_arg = strtoul_or_err(optarg, _("invalid argument to --cdspeed/-x option"));
			break;
		case 'd':
			d_option = 1;
			break;
		case 'f':
			f_option = 1;
			break;
		case 'h':
			usage(stdout);
			break;
		case 'm':
			m_option = 1;
			break;
		case 'n':
			n_option = 1;
			break;
		case 'p':
			p_option = 1;
			break;
		case 'q':
			q_option = 1;
			break;
		case 'r':
			r_option = 1;
			break;
		case 's':
			s_option = 1;
			break;
		case 't':
			t_option = 1;
			break;
		case 'T':
			T_option = 1;
			break;
		case 'v':
			v_option = 1;
			break;
		case 'V':
			printf(UTIL_LINUX_VERSION);
			exit(EXIT_SUCCESS);
			break;
		default:
		case '?':
			usage(stderr);
			break;
		}
	}

	/* check for a single additional argument */
	if ((argc - optind) > 1)
		errx(EXIT_FAILURE, _("too many arguments"));

	if ((argc - optind) == 1)
		*device = xstrdup(argv[optind]);
}

/*
 * Given name, such as foo, see if any of the following exist:
 *
 * foo (if foo starts with '.' or '/')
 * /dev/foo
 *
 * If found, return the full path. If not found, return 0.
 * Returns pointer to dynamically allocated string.
 */
static char *find_device(const char *name)
{
	if ((*name == '.' || *name == '/') && access(name, F_OK) == 0)
		return xstrdup(name);
	else {
		char buf[PATH_MAX];

		snprintf(buf, sizeof(buf), "/dev/%s", name);
		if (access(name, F_OK) == 0)
			return xstrdup(buf);
	}

	return NULL;
}

/* Set or clear auto-eject mode. */
static void auto_eject(int fd, int on)
{
	if (ioctl(fd, CDROMEJECT_SW, on) != 0)
		err(EXIT_FAILURE, _("CD-ROM auto-eject command failed"));
}


/*
 * Changer select. CDROM_SELECT_DISC is preferred, older kernels used
 * CDROMLOADFROMSLOT.
 */
static void changer_select(int fd, int slot)
{
#ifdef CDROM_SELECT_DISC
	if (ioctl(fd, CDROM_SELECT_DISC, slot) < 0)
		err(EXIT_FAILURE, _("CD-ROM select disc command failed"));

#elif defined CDROMLOADFROMSLOT
	if (ioctl(fd, CDROMLOADFROMSLOT, slot) != 0)
		err(EXIT_FAILURE, _("CD-ROM load from slot command failed"));
#else
	warnx(_("IDE/ATAPI CD-ROM changer not supported by this kernel\n") );
#endif
}

/*
 * Close tray. Not supported by older kernels.
 */
static void close_tray(int fd)
{
#ifdef CDROMCLOSETRAY
	if (ioctl(fd, CDROMCLOSETRAY) != 0)
		err(EXIT_FAILURE, _("CD-ROM tray close command failed"));
#else
	warnx(_("CD-ROM tray close command not supported by this kernel\n"));
#endif
}

/*
 * Toggle tray.
 *
 * Written by Benjamin Schwenk <benjaminschwenk@yahoo.de> and
 * Sybren Stuvel <sybren@thirdtower.com>
 *
 * Not supported by older kernels because it might use
 * CloseTray().
 *
 */
static void toggle_tray(int fd)
{
	struct timeval time_start, time_stop;
	int time_elapsed;

#ifdef CDROMCLOSETRAY
	/* Try to open the CDROM tray and measure the time therefor
	 * needed.  In my experience the function needs less than 0.05
	 * seconds if the tray was already open, and at least 1.5 seconds
	 * if it was closed.  */
	gettimeofday(&time_start, NULL);

	/* Send the CDROMEJECT command to the device. */
	if (ioctl(fd, CDROMEJECT, 0) < 0)
		err(EXIT_FAILURE, _("CD-ROM eject command failed"));

	/* Get the second timestamp, to measure the time needed to open
	 * the tray.  */
	gettimeofday(&time_stop, NULL);

	time_elapsed = (time_stop.tv_sec * 1000000 + time_stop.tv_usec) -
		(time_start.tv_sec * 1000000 + time_start.tv_usec);

	/* If the tray "opened" too fast, we can be nearly sure, that it
	 * was already open. In this case, close it now. Else the tray was
	 * closed before. This would mean that we are done.  */
	if (time_elapsed < TRAY_WAS_ALREADY_OPEN_USECS)
		close_tray(fd);
#else
	warnx(_("CD-ROM tray toggle command not supported by this kernel"));
#endif

}

/*
 * Select Speed of CD-ROM drive.
 * Thanks to Roland Krivanek (krivanek@fmph.uniba.sk)
 * http://dmpc.dbp.fmph.uniba.sk/~krivanek/cdrom_speed/
 */
static void select_speed(int fd, int speed)
{
#ifdef CDROM_SELECT_SPEED
	if (ioctl(fd, CDROM_SELECT_SPEED, speed) != 0)
		err(EXIT_FAILURE, _("CD-ROM select speed command failed"));
#else
	warnx(_("CD-ROM select speed command not supported by this kernel"));
#endif
}

/*
 * Eject using CDROMEJECT ioctl. Return 1 if successful, 0 otherwise.
 */
static int eject_cdrom(int fd)
{
	return ioctl(fd, CDROMEJECT) == 0;
}

/*
 * Eject using SCSI commands. Return 1 if successful, 0 otherwise.
 */
static int eject_scsi(int fd)
{
	struct sdata {
		int  inlen;
		int  outlen;
		char cmd[256];
	} scsi_cmd;

	scsi_cmd.inlen	= 0;
	scsi_cmd.outlen = 0;
	scsi_cmd.cmd[0] = ALLOW_MEDIUM_REMOVAL;
	scsi_cmd.cmd[1] = 0;
	scsi_cmd.cmd[2] = 0;
	scsi_cmd.cmd[3] = 0;
	scsi_cmd.cmd[4] = 0;
	scsi_cmd.cmd[5] = 0;
	if (ioctl(fd, SCSI_IOCTL_SEND_COMMAND, (void *)&scsi_cmd) != 0)
		return 0;

	scsi_cmd.inlen	= 0;
	scsi_cmd.outlen = 0;
	scsi_cmd.cmd[0] = START_STOP;
	scsi_cmd.cmd[1] = 0;
	scsi_cmd.cmd[2] = 0;
	scsi_cmd.cmd[3] = 0;
	scsi_cmd.cmd[4] = 1;
	scsi_cmd.cmd[5] = 0;
	if (ioctl(fd, SCSI_IOCTL_SEND_COMMAND, (void *)&scsi_cmd) != 0)
		return 0;

	scsi_cmd.inlen	= 0;
	scsi_cmd.outlen = 0;
	scsi_cmd.cmd[0] = START_STOP;
	scsi_cmd.cmd[1] = 0;
	scsi_cmd.cmd[2] = 0;
	scsi_cmd.cmd[3] = 0;
	scsi_cmd.cmd[4] = 2;
	scsi_cmd.cmd[5] = 0;
	if (ioctl(fd, SCSI_IOCTL_SEND_COMMAND, (void *)&scsi_cmd) != 0)
		return 0;

	/* force kernel to reread partition table when new disc inserted */
	return ioctl(fd, BLKRRPART) == 0;
}


/*
 * Eject using FDEJECT ioctl. Return 1 if successful, 0 otherwise.
 */
static int eject_floppy(int fd)
{
	return ioctl(fd, FDEJECT) == 0;
}


/*
 * Rewind and eject using tape ioctl. Return 1 if successful, 0 otherwise.
 */
static int eject_tape(int fd)
{
	struct mtop op = { .mt_op = MTOFFL, .mt_count = 0 };

	return ioctl(fd, MTIOCTOP, &op) == 0;
}


/* Unmount a device. */
static void unmount_one(const char *name)
{
	int status;

	switch (fork()) {
	case 0: /* child */
		if (setgid(getgid()) < 0)
			err(EXIT_FAILURE, _("cannot set group id"));

		if (setuid(getuid()) < 0)
			err(EXIT_FAILURE, _("eject: cannot set user id"));

		if (p_option)
			execl("/bin/umount", "/bin/umount", name, "-n", NULL);
		else
			execl("/bin/umount", "/bin/umount", name, NULL);

		errx(EXIT_FAILURE, _("unable to exec /bin/umount of `%s'"), name);

	case -1:
		warn( _("unable to fork"));
		break;

	default: /* parent */
		wait(&status);
		if (WIFEXITED(status) == 0)
			errx(EXIT_FAILURE,
			     _("unmount of `%s' did not exit normally"), name);

		if (WEXITSTATUS(status) != 0)
			errx(EXIT_FAILURE, _("unmount of `%s' failed\n"), name);
		break;
	}
}


/* Open a device file. */
static int open_device(const char *name)
{
	int fd = open(name, O_RDONLY|O_NONBLOCK);
	if (fd == -1)
		errx(EXIT_FAILURE, _("%s: open failed"), name);
	return fd;
}


/*
 * Get major and minor device numbers for a device file name, so we
 * can check for duplicate devices.
 */
static int get_major_minor(const char *name, int *maj, int *min)
{
	struct stat sstat;

	*maj = *min = -1;
	if (stat(name, &sstat) == -1)
		return -1;
	if (!S_ISBLK(sstat.st_mode))
		return -1;
	*maj = major(sstat.st_rdev);
	*min = minor(sstat.st_rdev);
	return 0;
}


/*
 * See if device has been mounted by looking in mount table.  If so, set
 * device name and mount point name, and return 1, otherwise return 0.
 *
 * TODO: use libmount here
 */
static int mounted_device(const char *name, char **mountName, char **deviceName)
{
	FILE *fp;
	const char *fname;
	char line[1024];
	char s1[1024];
	char s2[1024];
	int rc;

	int maj;
	int min;

	get_major_minor(name, &maj, &min);

	fname = p_option ? "/proc/mounts" : "/etc/mtab";

	fp = fopen(fname, "r");
	if (!fp)
		err(EXIT_FAILURE, _("%s: open failed"), fname);

	while (fgets(line, sizeof(line), fp) != 0) {
		rc = sscanf(line, "%1023s %1023s", s1, s2);
		if (rc >= 2) {
			int mtabmaj, mtabmin;
			get_major_minor(s1, &mtabmaj, &mtabmin);
			if (((strcmp(s1, name) == 0) || (strcmp(s2, name) == 0)) ||
				((maj != -1) && (maj == mtabmaj) && (min == mtabmin))) {
				fclose(fp);
				*deviceName = xstrdup(s1);
				*mountName = xstrdup(s2);
				return 1;
			}
		}
	}
	*deviceName = 0;
	*mountName = 0;
	fclose(fp);
	return 0;
}

/*
 * Step through mount table and unmount all devices that match a regular
 * expression.
 *
 * TODO: replace obscure regex voodoo with /sys scan
 */
static void unmount_devices(const char *pattern)
{
	regex_t preg;
	FILE *fp;
	const char *fname;
	char line[1024];

	if (regcomp(&preg, pattern, REG_EXTENDED) != 0)
		err(EXIT_FAILURE, _("regcomp failed"));

	fname = p_option ? "/proc/mounts" : "/etc/mtab";
	fp = fopen(fname, "r");
	if (!fp)
		err(EXIT_FAILURE, _("%s: open failed"), fname);

	while (fgets(line, sizeof(line), fp) != 0) {
		char s1[1024];
		char s2[1024];

		int status = sscanf(line, "%1023s %1023s", s1, s2);
		if (status >= 2) {
			status = regexec(&preg, s1, 0, 0, 0);
			if (status == 0) {
				if (v_option)
					printf(_("%s: unmounting `%s'\n"), program_invocation_short_name, s1);
				unmount_one(s1);
				regfree(&preg);
			}
		}
	}
	fclose(fp);
}

/*
 * Given a name, see if it matches a pattern for a device that can have
 * multiple partitions.  If so, return a regular expression that matches
 * partitions for that device, otherwise return 0.
 */
static char *multiple_partitions(const char *name) {
	int i = 0;
	int status;
	regex_t preg;
	char pattern[256];
	char *result = 0;

	for (i = 0; partition_device[i] != 0; i++) {
		/* look for ^/dev/foo[a-z]([0-9]?[0-9])?$, e.g. /dev/hda1 */
		strcpy(pattern, "^/dev/");
		strcat(pattern, partition_device[i]);
		strcat(pattern, "[a-z]([0-9]?[0-9])?$");
		regcomp(&preg, pattern, REG_EXTENDED|REG_NOSUB);
		status = regexec(&preg, name, 1, 0, 0);
		regfree(&preg);
		if (status == 0) {
			result = (char *) malloc(strlen(name) + 25);
			strcpy(result, name);
			result[strlen(partition_device[i]) + 6] = 0;
			strcat(result, "([0-9]?[0-9])?$");
			if (v_option)
				printf(_("%s: `%s' is a multipartition device\n"), program_invocation_short_name, name);
			return result;
		}
	}
	if (v_option)
		printf(_("%s: `%s' is not a multipartition device\n"), program_invocation_short_name, name);
	return 0;
}


/* handle -x option */
void handle_x_option(char *deviceName) {
	int fd; 	   /* file descriptor for device */
	if (x_option) {
		if (v_option)
		{
			if (x_arg == 0)
				printf(_("%s: setting CD-ROM speed to auto\n"),
						program_invocation_short_name);
			else
				printf(_("%s: setting CD-ROM speed to %ldX\n"),
						program_invocation_short_name,
						x_arg);
		}
		fd = open_device(deviceName);
		select_speed(fd, x_arg);
		exit(0);
	}
}


/* main program */
int main(int argc, char **argv) {

	const char *defaultDevice = EJECT_DEFAULT_DEVICE;  /* default if no name passed by user */
	int worked = 0;    /* set to 1 when successfully ejected */
	char *device = 0;  /* name passed from user */
	char *fullName;    /* expanded name */
	char *deviceName;  /* name of device */
	char *linkName;    /* name of device's symbolic link */
	char *mountName;   /* name of device's mount point */
	int fd;            /* file descriptor for device */
	int mounted = 0;   /* true if device is mounted */
	char *pattern;     /* regex for device if multiple partitions */
	int ld = 6;        /* symbolic link max depth */

	setlocale(LC_ALL,"");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* program name is global variable used by other procedures */
	char *programName = program_invocation_short_name;

	/* parse the command line arguments */
	parse_args(argc, argv, &device);


	/* handle -d option */
	if (d_option) {
		printf(_("%s: default device: `%s'\n"), programName, defaultDevice);
		exit(0);
	}

	/* if no device, use default */
	if (device == 0) {
		device = strdup(defaultDevice);
		if (v_option)
			printf(_("%s: using default device `%s'\n"), programName, device);
	}

	/* Strip any trailing slash from name in case user used bash/tcsh
	   style filename completion (e.g. /mnt/cdrom/) */
	if (device[strlen(device)-1] == '/')
		device[strlen(device)-1] = 0;

	if (v_option)
		printf(_("%s: device name is `%s'\n"), programName, device);

	/* figure out full device or mount point name */
	fullName = find_device(device);
	if (fullName == 0) {
		errx(1, _("unable to find or open device for: `%s'"), device);
	}
	if (v_option)
		printf(_("%s: expanded name is `%s'\n"), programName, fullName);

	/* check for a symbolic link */
	while ((linkName = canonicalize_path(fullName)) && (ld > 0)) {
		if (v_option)
			printf(_("%s: `%s' is a link to `%s'\n"), programName, fullName, linkName);
		free(fullName);
		fullName = strdup(linkName);
		free(linkName);
		linkName = 0;
		ld--;
	}
	/* handle max depth exceeded option */
	if (ld <= 0) {
		errx(1, _("maximum symbolic link depth exceeded: `%s'"), fullName);
	}

	/* if mount point, get device name */
	mounted = mounted_device(fullName, &mountName, &deviceName);
	if (v_option) {
		if (mounted)
			printf(_("%s: `%s' is mounted at `%s'\n"), programName, deviceName, mountName);
		else
			printf(_("%s: `%s' is not mounted\n"), programName, fullName);
	}
	if (!mounted) {
		deviceName = strdup(fullName);
	}

	/* handle -n option */
	if (n_option) {
		printf(_("%s: device is `%s'\n"), programName, deviceName);
		if (v_option)
			printf(_("%s: exiting due to -n/--noop option\n"), programName);
		exit(0);
	}

	/* handle -a option */
	if (a_option) {
		if (v_option) {
			if (a_arg)
				printf(_("%s: enabling auto-eject mode for `%s'\n"), programName, deviceName);
			else
				printf(_("%s: disabling auto-eject mode for `%s'\n"), programName, deviceName);
		}
		fd = open_device(deviceName);
		auto_eject(fd, a_arg);
		exit(0);
	}

	/* handle -t option */
	if (t_option) {
		if (v_option)
			printf(_("%s: closing tray\n"), programName);
		fd = open_device(deviceName);
		close_tray(fd);
		handle_x_option(deviceName);
		exit(0);
	}

	/* handle -T option */
	if (T_option) {
		if (v_option)
			printf(_("%s: toggling tray\n"), programName);
		fd = open_device(deviceName);
		toggle_tray(fd);
		handle_x_option(deviceName);
		exit(0);
	}

	/* handle -x option only */
	if (!c_option) handle_x_option(deviceName);

	/* unmount device if mounted */
	if ((m_option != 1) && mounted) {
		if (v_option)
			printf(_("%s: unmounting `%s'\n"), programName, deviceName);
		unmount_one(deviceName);
	}

	/* if it is a multipartition device, unmount any other partitions on
	   the device */
	pattern = multiple_partitions(deviceName);
	if ((m_option != 1) && (pattern != 0))
		unmount_devices(pattern);

	/* handle -c option */
	if (c_option) {
		if (v_option)
			printf(_("%s: selecting CD-ROM disc #%ld\n"), programName, c_arg);
		fd = open_device(deviceName);
		changer_select(fd, c_arg);
		handle_x_option(deviceName);
		exit(0);
	}

	/* if user did not specify type of eject, try all four methods */
	if ((r_option + s_option + f_option + q_option) == 0) {
		r_option = s_option = f_option = q_option = 1;
	}

	/* open device */
	fd = open_device(deviceName);

	/* try various methods of ejecting until it works */
	if (r_option) {
		if (v_option)
			printf(_("%s: trying to eject `%s' using CD-ROM eject command\n"), programName, deviceName);
		worked = eject_cdrom(fd);
		if (v_option) {
			if (worked)
				printf(_("%s: CD-ROM eject command succeeded\n"), programName);
			else
				printf(_("%s: CD-ROM eject command failed\n"), programName);
		}
	}

	if (s_option && !worked) {
		if (v_option)
			printf(_("%s: trying to eject `%s' using SCSI commands\n"), programName, deviceName);
		worked = eject_scsi(fd);
		if (v_option) {
			if (worked)
				printf(_("%s: SCSI eject succeeded\n"), programName);
			else
				printf(_("%s: SCSI eject failed\n"), programName);
		}
	}

	if (f_option && !worked) {
		if (v_option)
			printf(_("%s: trying to eject `%s' using floppy eject command\n"), programName, deviceName);
		worked = eject_floppy(fd);
		if (v_option) {
			if (worked)
				printf(_("%s: floppy eject command succeeded\n"), programName);
			else
				printf(_("%s: floppy eject command failed\n"), programName);
		}
	}

	if (q_option && !worked) {
		if (v_option)
			printf(_("%s: trying to eject `%s' using tape offline command\n"), programName, deviceName);
		worked = eject_tape(fd);
		if (v_option) {
			if (worked)
				printf(_("%s: tape offline command succeeded\n"), programName);
			else
				printf(_("%s: tape offline command failed\n"), programName);
		}
	}

	if (!worked) {
		err(1, _("unable to eject, last error"));
	}

	/* cleanup */
	close(fd);
	free(device);
	free(deviceName);
	free(fullName);
	free(linkName);
	free(mountName);
	free(pattern);
	exit(0);
}
