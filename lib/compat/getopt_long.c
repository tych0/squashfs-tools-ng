/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * getopt_long.c
 *
 * Copyright (C) 2019 David Oberhollenzer <goliath@infraroot.at>
 */
#include "config.h"
#include "compat.h"

#ifdef HAVE_GETOPT
#include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>

#ifndef HAVE_GETOPT_LONG
int getopt_long(int argc, char *const argv[], const char *optstr,
		const struct option *longopts, int *longindex)
{
	size_t i, len;
	char *ptr;
	(void)longindex;

	if (optind >= argc || argv[optind][0] != '-' ||
	    argv[optind][1] != '-' || argv[optind][2] == '\0')
		return getopt(argc, argv, optstr);

	optarg = NULL;
	ptr = argv[optind] + 2;

	for (len = 0; ptr[len] != '\0' && ptr[len] != '='; ++len)
		;

	for (i = 0; longopts[i].name != NULL; ++i) {
		if (strncmp(longopts[i].name, ptr, len) != 0)
			continue;

		if (longopts[i].name[len] == '\0')
			break;
	}

	if (longopts[i].name == NULL)
		goto fail_unknown;

	if (ptr[len] == '=') {
		if (!longopts[i].has_arg)
			goto fail_arg;

		optarg = ptr + len + 1;
	} else if (longopts[i].has_arg) {
		if (++optind >= argc)
			goto fail_no_arg;

		optarg = argv[optind];
	}

	++optind;
	return longopts[i].val;
fail_unknown:
	fprintf(stderr, "%s: unknown option `--%s`\n", argv[0], ptr);
	return '?';
fail_arg:
	fprintf(stderr, "%s: no argument expected for option `--%s`\n",
		argv[0], ptr);
	return '?';
fail_no_arg:
	fprintf(stderr, "%s: missing argument for option `--%s`\n",
		argv[0], ptr);
	return '?';
}
#endif
