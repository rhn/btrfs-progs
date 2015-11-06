/*
 * Copyright (C) 2012 STRATO.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <sys/ioctl.h>
#include <unistd.h>
#include <getopt.h>

#include "ctree.h"
#include "ioctl.h"

#include "commands.h"
#include "qgroup.h"
#include "utils.h"

static const char * const qgroup_cmd_group_usage[] = {
	"btrfs qgroup <command> [options] <path>",
	NULL
};

static int qgroup_assign(int assign, int argc, char **argv)
{
	int ret = 0;
	int fd;
	int e;
	int rescan = 0;
	char *path;
	struct btrfs_ioctl_qgroup_assign_args args;
	DIR *dirstream = NULL;

	while (1) {
		enum { GETOPT_VAL_RESCAN = 256 };
		static const struct option long_options[] = {
			{ "rescan", no_argument, NULL, GETOPT_VAL_RESCAN },
			{ NULL, 0, NULL, 0 }
		};
		int c = getopt_long(argc, argv, "", long_options, NULL);

		if (c < 0)
			break;
		switch (c) {
		case GETOPT_VAL_RESCAN:
			rescan = 1;
			break;
		default:
			/* Usage printed by the caller */
			return -1;
		}
	}

	if (check_argc_exact(argc - optind, 3))
		return -1;

	memset(&args, 0, sizeof(args));
	args.assign = assign;
	args.src = parse_qgroupid(argv[optind]);
	args.dst = parse_qgroupid(argv[optind + 1]);

	path = argv[optind + 2];

	/*
	 * FIXME src should accept subvol path
	 */
	if (btrfs_qgroup_level(args.src) >= btrfs_qgroup_level(args.dst)) {
		fprintf(stderr, "ERROR: bad relation requested '%s'\n", path);
		return 1;
	}
	fd = btrfs_open_dir(path, &dirstream, 1);
	if (fd < 0)
		return 1;

	ret = ioctl(fd, BTRFS_IOC_QGROUP_ASSIGN, &args);
	e = errno;
	if (ret < 0) {
		fprintf(stderr, "ERROR: unable to assign quota group: %s\n",
			strerror(e));
		close_file_or_dir(fd, dirstream);
		return 1;
	}

	/*
	 * If ret > 0, it means assign caused qgroup data inconsistent state.
	 * Schedule a quota rescan if requested.
	 *
	 * The return value change only happens in newer kernel. But will not
	 * cause problem since old kernel has a bug that will never clear
	 * INCONSISTENT bit.
	 */
	if (ret > 0) {
		if (rescan) {
			struct btrfs_ioctl_quota_rescan_args args;

			printf("Quota data changed, rescan scheduled\n");
			memset(&args, 0, sizeof(args));
			ret = ioctl(fd, BTRFS_IOC_QUOTA_RESCAN, &args);
			if (ret < 0)
				fprintf(stderr,
					"ERROR: quota rescan failed: %s\n",
					strerror(errno));
		} else {
			printf("WARNING: quotas may be inconsistent, rescan needed\n");
		}
	}
	close_file_or_dir(fd, dirstream);
	return ret;
}

static int qgroup_create(int create, int argc, char **argv)
{
	int ret = 0;
	int fd;
	int e;
	char *path = argv[2];
	struct btrfs_ioctl_qgroup_create_args args;
	DIR *dirstream = NULL;

	if (check_argc_exact(argc, 3))
		return -1;

	memset(&args, 0, sizeof(args));
	args.create = create;
	args.qgroupid = parse_qgroupid(argv[1]);

	fd = btrfs_open_dir(path, &dirstream, 1);
	if (fd < 0)
		return 1;

	ret = ioctl(fd, BTRFS_IOC_QGROUP_CREATE, &args);
	e = errno;
	close_file_or_dir(fd, dirstream);
	if (ret < 0) {
		fprintf(stderr, "ERROR: unable to %s quota group: %s\n",
			create ? "create":"destroy", strerror(e));
		return 1;
	}
	return 0;
}

static int parse_limit(const char *p, unsigned long long *s)
{
	char *endptr;
	unsigned long long size;
	unsigned long long CLEAR_VALUE = -1;

	if (strcasecmp(p, "none") == 0) {
		*s = CLEAR_VALUE;
		return 1;
	}

	if (p[0] == '-')
		return 0;

	size = strtoull(p, &endptr, 10);
	if (p == endptr)
		return 0;

	switch (*endptr) {
	case 'T':
	case 't':
		size *= 1024;
		/* fallthrough */
	case 'G':
	case 'g':
		size *= 1024;
		/* fallthrough */
	case 'M':
	case 'm':
		size *= 1024;
		/* fallthrough */
	case 'K':
	case 'k':
		size *= 1024;
		++endptr;
		break;
	case 0:
		break;
	default:
		return 0;
	}

	if (*endptr)
		return 0;

	*s = size;

	return 1;
}

static const char * const cmd_qgroup_assign_usage[] = {
	"btrfs qgroup assign [options] <src> <dst> <path>",
	"Assign SRC as the child qgroup of DST",
	"",
	"--rescan       schedule qutoa rescan if needed",
	"--no-rescan    ",
	NULL
};

static int cmd_qgroup_assign(int argc, char **argv)
{
	int ret = qgroup_assign(1, argc, argv);
	if (ret < 0)
		usage(cmd_qgroup_assign_usage);
	return ret;
}

static const char * const cmd_qgroup_remove_usage[] = {
	"btrfs qgroup remove <src> <dst> <path>",
	"Remove a child qgroup SRC from DST.",
	NULL
};

static int cmd_qgroup_remove(int argc, char **argv)
{
	int ret = qgroup_assign(0, argc, argv);
	if (ret < 0)
		usage(cmd_qgroup_remove_usage);
	return ret;
}

static const char * const cmd_qgroup_create_usage[] = {
	"btrfs qgroup create <qgroupid> <path>",
	"Create a subvolume quota group.",
	NULL
};

static int cmd_qgroup_create(int argc, char **argv)
{
	int ret = qgroup_create(1, argc, argv);
	if (ret < 0)
		usage(cmd_qgroup_create_usage);
	return ret;
}

static const char * const cmd_qgroup_destroy_usage[] = {
	"btrfs qgroup destroy <qgroupid> <path>",
	"Destroy a quota group.",
	NULL
};

static int cmd_qgroup_destroy(int argc, char **argv)
{
	int ret = qgroup_create(0, argc, argv);
	if (ret < 0)
		usage(cmd_qgroup_destroy_usage);
	return ret;
}

static const char * const cmd_qgroup_show_usage[] = {
	"btrfs qgroup show -pcreFf "
	"[--sort=qgroupid,rfer,excl,max_rfer,max_excl] <path>",
	"Show subvolume quota groups.",
	"-p             print parent qgroup id",
	"-c             print child qgroup id",
	"-r             print limit of referenced size of qgroup",
	"-e             print limit of exclusive size of qgroup",
	"-F             list all qgroups which impact the given path",
	"               (including ancestral qgroups)",
	"-f             list all qgroups which impact the given path",
	"               (excluding ancestral qgroups)",
	HELPINFO_UNITS_LONG,
	"--sort=qgroupid,rfer,excl,max_rfer,max_excl",
	"               list qgroups sorted by specified items",
	"               you can use '+' or '-' in front of each item.",
	"               (+:ascending, -:descending, ascending default)",
	NULL
};

static int cmd_qgroup_show(int argc, char **argv)
{
	char *path;
	int ret = 0;
	int fd;
	int e;
	DIR *dirstream = NULL;
	u64 qgroupid;
	int filter_flag = 0;
	unsigned unit_mode;

	struct btrfs_qgroup_comparer_set *comparer_set;
	struct btrfs_qgroup_filter_set *filter_set;
	filter_set = btrfs_qgroup_alloc_filter_set();
	comparer_set = btrfs_qgroup_alloc_comparer_set();

	unit_mode = get_unit_mode_from_arg(&argc, argv, 0);

	optind = 1;
	while (1) {
		int c;
		static const struct option long_options[] = {
			{"sort", required_argument, NULL, 'S'},
			{ NULL, 0, NULL, 0 }
		};

		c = getopt_long(argc, argv, "pcreFf", long_options, NULL);
		if (c < 0)
			break;
		switch (c) {
		case 'p':
			btrfs_qgroup_setup_print_column(
				BTRFS_QGROUP_PARENT);
			break;
		case 'c':
			btrfs_qgroup_setup_print_column(
				BTRFS_QGROUP_CHILD);
			break;
		case 'r':
			btrfs_qgroup_setup_print_column(
				BTRFS_QGROUP_MAX_RFER);
			break;
		case 'e':
			btrfs_qgroup_setup_print_column(
				BTRFS_QGROUP_MAX_EXCL);
			break;
		case 'F':
			filter_flag |= 0x1;
			break;
		case 'f':
			filter_flag |= 0x2;
			break;
		case 'S':
			ret = btrfs_qgroup_parse_sort_string(optarg,
							     &comparer_set);
			if (ret)
				usage(cmd_qgroup_show_usage);
			break;
		default:
			usage(cmd_qgroup_show_usage);
		}
	}
	btrfs_qgroup_setup_units(unit_mode);

	if (check_argc_exact(argc - optind, 1))
		usage(cmd_qgroup_show_usage);

	path = argv[optind];
	fd = btrfs_open_dir(path, &dirstream, 1);
	if (fd < 0) {
		btrfs_qgroup_free_filter_set(filter_set);
		btrfs_qgroup_free_comparer_set(comparer_set);
		return 1;
	}

	if (filter_flag) {
		qgroupid = btrfs_get_path_rootid(fd);
		if (filter_flag & 0x1)
			btrfs_qgroup_setup_filter(&filter_set,
					BTRFS_QGROUP_FILTER_ALL_PARENT,
					qgroupid);
		if (filter_flag & 0x2)
			btrfs_qgroup_setup_filter(&filter_set,
					BTRFS_QGROUP_FILTER_PARENT,
					qgroupid);
	}
	ret = btrfs_show_qgroups(fd, filter_set, comparer_set);
	e = errno;
	close_file_or_dir(fd, dirstream);
	if (ret < 0)
		fprintf(stderr, "ERROR: can't list qgroups: %s\n",
				strerror(e));

	return !!ret;
}

static const char * const cmd_qgroup_limit_usage[] = {
	"btrfs qgroup limit [options] <size>|none [<qgroupid>] <path>",
	"Set the limits a subvolume quota group.",
	"",
	"-c   limit amount of data after compression. This is the default,",
	"     it is currently not possible to turn off this option.",
	"-e   limit space exclusively assigned to this qgroup",
	NULL
};

static int cmd_qgroup_limit(int argc, char **argv)
{
	int ret = 0;
	int fd;
	int e;
	char *path = NULL;
	struct btrfs_ioctl_qgroup_limit_args args;
	unsigned long long size;
	int compressed = 0;
	int exclusive = 0;
	DIR *dirstream = NULL;

	optind = 1;
	while (1) {
		int c = getopt(argc, argv, "ce");
		if (c < 0)
			break;
		switch (c) {
		case 'c':
			compressed = 1;
			break;
		case 'e':
			exclusive = 1;
			break;
		default:
			usage(cmd_qgroup_limit_usage);
		}
	}

	if (check_argc_min(argc - optind, 2))
		usage(cmd_qgroup_limit_usage);

	if (!parse_limit(argv[optind], &size)) {
		fprintf(stderr, "Invalid size argument given\n");
		return 1;
	}

	memset(&args, 0, sizeof(args));
	if (compressed)
		args.lim.flags |= BTRFS_QGROUP_LIMIT_RFER_CMPR |
				  BTRFS_QGROUP_LIMIT_EXCL_CMPR;
	if (exclusive) {
		args.lim.flags |= BTRFS_QGROUP_LIMIT_MAX_EXCL;
		args.lim.max_exclusive = size;
	} else {
		args.lim.flags |= BTRFS_QGROUP_LIMIT_MAX_RFER;
		args.lim.max_referenced = size;
	}

	if (argc - optind == 2) {
		args.qgroupid = 0;
		path = argv[optind + 1];
		ret = test_issubvolume(path);
		if (ret < 0) {
			fprintf(stderr, "ERROR: error accessing '%s'\n", path);
			return 1;
		}
		if (!ret) {
			fprintf(stderr, "ERROR: '%s' is not a subvolume\n",
				path);
			return 1;
		}
		/*
		 * keep qgroupid at 0, this indicates that the subvolume the
		 * fd refers to is to be limited
		 */
	} else if (argc - optind == 3) {
		args.qgroupid = parse_qgroupid(argv[optind + 1]);
		path = argv[optind + 2];
	} else
		usage(cmd_qgroup_limit_usage);

	fd = btrfs_open_dir(path, &dirstream, 1);
	if (fd < 0)
		return 1;

	ret = ioctl(fd, BTRFS_IOC_QGROUP_LIMIT, &args);
	e = errno;
	close_file_or_dir(fd, dirstream);
	if (ret < 0) {
		fprintf(stderr, "ERROR: unable to limit requested quota group: "
			"%s\n", strerror(e));
		return 1;
	}
	return 0;
}

static const char qgroup_cmd_group_info[] =
"manage quota groups";

const struct cmd_group qgroup_cmd_group = {
	qgroup_cmd_group_usage, qgroup_cmd_group_info, {
		{ "assign", cmd_qgroup_assign, cmd_qgroup_assign_usage,
		   NULL, 0 },
		{ "remove", cmd_qgroup_remove, cmd_qgroup_remove_usage,
		   NULL, 0 },
		{ "create", cmd_qgroup_create, cmd_qgroup_create_usage,
		   NULL, 0 },
		{ "destroy", cmd_qgroup_destroy, cmd_qgroup_destroy_usage,
		   NULL, 0 },
		{ "show", cmd_qgroup_show, cmd_qgroup_show_usage,
		   NULL, 0 },
		{ "limit", cmd_qgroup_limit, cmd_qgroup_limit_usage,
		   NULL, 0 },
		NULL_CMD_STRUCT
	}
};

int cmd_qgroup(int argc, char **argv)
{
	return handle_command_group(&qgroup_cmd_group, argc, argv);
}
