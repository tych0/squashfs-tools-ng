/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * getopt.c
 *
 * Copyright (C) 2019 David Oberhollenzer <goliath@infraroot.at>
 */
#include "config.h"
#include "compat.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifndef HAVE_GETOPT
static char *current = "";
int optind = 1;
char *optarg = NULL;

int getopt(int argc, char *const argv[], const char *optstr)
{
	char optchr, *ptr;

	if (*current == '\0') {
		if (optind >= argc || argv[optind][0] != '-')
			return -1;

		if (argv[optind][1] == '-' && argv[optind][2] == '\0') {
			++optind;
			return -1;
		}

		if (argv[optind][1] == '\0')
			return -1;

		current = argv[optind] + 1;
	}

	optchr = *(current++);
	if (optchr == ':' || (ptr = strchr(optstr, optchr)) == NULL)
		goto fail_unknown;

	if (ptr[1] == ':') {
		if (*current != '\0') {
			optarg = current;
		} else {
			if (++optind >= argc)
				goto fail_arg;

			optarg = argv[optind];
		}

		current = "";
		++optind;
	} else {
		optarg = NULL;

		if (*current == '\0')
			++optind;
	}

	return optchr;
fail_unknown:
	fprintf(stderr, "%s: unknown option `-%c`\n", argv[0], optchr);
	return '?';
fail_arg:
	fprintf(stderr, "%s: missing argument for option `-%c`\n",
		argv[0], optchr);
	return '?';
}
#endif
