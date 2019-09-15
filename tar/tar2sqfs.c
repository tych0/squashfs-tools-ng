/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tar2sqfs.c
 *
 * Copyright (C) 2019 David Oberhollenzer <goliath@infraroot.at>
 */
#include "config.h"

#include "sqfs/compress.h"
#include "sqfs/id_table.h"
#include "sqfs/xattr.h"
#include "sqfs/data.h"
#include "sqfs/io.h"

#include "data_writer.h"
#include "highlevel.h"
#include "fstree.h"
#include "util.h"
#include "tar.h"

#include <sys/sysmacros.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

static struct option long_opts[] = {
	{ "compressor", required_argument, NULL, 'c' },
	{ "block-size", required_argument, NULL, 'b' },
	{ "dev-block-size", required_argument, NULL, 'B' },
	{ "defaults", required_argument, NULL, 'd' },
	{ "num-jobs", required_argument, NULL, 'j' },
	{ "queue-backlog", required_argument, NULL, 'Q' },
	{ "comp-extra", required_argument, NULL, 'X' },
	{ "no-skip", no_argument, NULL, 's' },
	{ "no-xattr", no_argument, NULL, 'x' },
	{ "keep-time", no_argument, NULL, 'k' },
	{ "exportable", no_argument, NULL, 'e' },
	{ "force", no_argument, NULL, 'f' },
	{ "quiet", no_argument, NULL, 'q' },
	{ "help", no_argument, NULL, 'h' },
	{ "version", no_argument, NULL, 'V' },
};

static const char *short_opts = "c:b:B:d:X:j:Q:sxekfqhV";

static const char *usagestr =
"Usage: tar2sqfs [OPTIONS...] <sqfsfile>\n"
"\n"
"Read an uncompressed tar archive from stdin and turn it into a squashfs\n"
"filesystem image.\n"
"\n"
"Possible options:\n"
"\n"
"  --compressor, -c <name>     Select the compressor to use.\n"
"                              A list of available compressors is below.\n"
"  --comp-extra, -X <options>  A comma seperated list of extra options for\n"
"                              the selected compressor. Specify 'help' to\n"
"                              get a list of available options.\n"
"  --num-jobs, -j <count>      Number of compressor jobs to create.\n"
"  --queue-backlog, -Q <count> Maximum number of data blocks in the thread\n"
"                              worker queue before the packer starts waiting\n"
"                              for the block processors to catch up.\n"
"                              Defaults to 10 times the number of jobs.\n"
"  --block-size, -b <size>     Block size to use for Squashfs image.\n"
"                              Defaults to %u.\n"
"  --dev-block-size, -B <size> Device block size to padd the image to.\n"
"                              Defaults to %u.\n"
"  --defaults, -d <options>    A comma seperated list of default values for\n"
"                              implicitly created directories.\n"
"\n"
"                              Possible options:\n"
"                                 uid=<value>    0 if not set.\n"
"                                 gid=<value>    0 if not set.\n"
"                                 mode=<value>   0755 if not set.\n"
"                                 mtime=<value>  0 if not set.\n"
"\n"
"  --no-skip, -s               Abort if a tar record cannot be read instead\n"
"                              of skipping it.\n"
"  --no-xattr, -x              Do not copy extended attributes from archive.\n"
"  --keep-time, -k             Keep the time stamps stored in the archive\n"
"                              instead of setting defaults on all files.\n"
"  --exportable, -e            Generate an export table for NFS support.\n"
"  --force, -f                 Overwrite the output file if it exists.\n"
"  --quiet, -q                 Do not print out progress reports.\n"
"  --help, -h                  Print help text and exit.\n"
"  --version, -V               Print version information and exit.\n"
"\n"
"Examples:\n"
"\n"
"\ttar2sqfs rootfs.sqfs < rootfs.tar\n"
"\tzcat rootfs.tar.gz | tar2sqfs rootfs.sqfs\n"
"\txzcat rootfs.tar.xz | tar2sqfs rootfs.sqfs\n"
"\n";

static const char *filename;
static int block_size = SQFS_DEFAULT_BLOCK_SIZE;
static size_t devblksize = SQFS_DEVBLK_SIZE;
static bool quiet = false;
static int outmode = 0;
static unsigned int num_jobs = 1;
static size_t max_backlog = 0;
static E_SQFS_COMPRESSOR comp_id;
static char *comp_extra = NULL;
static char *fs_defaults = NULL;
static bool dont_skip = false;
static bool no_xattr = false;
static bool exportable = false;
static bool keep_time = false;

static void process_args(int argc, char **argv)
{
	bool have_compressor;
	int i;

	comp_id = compressor_get_default();

	for (;;) {
		i = getopt_long(argc, argv, short_opts, long_opts, NULL);
		if (i == -1)
			break;

		switch (i) {
		case 'b':
			block_size = strtol(optarg, NULL, 0);
			break;
		case 'B':
			devblksize = strtol(optarg, NULL, 0);
			if (devblksize < 1024) {
				fputs("Device block size must be at "
				      "least 1024\n", stderr);
				exit(EXIT_FAILURE);
			}
			break;
		case 'c':
			have_compressor = true;

			if (sqfs_compressor_id_from_name(optarg, &comp_id))
				have_compressor = false;

			if (!sqfs_compressor_exists(comp_id))
				have_compressor = false;

			if (!have_compressor) {
				fprintf(stderr, "Unsupported compressor '%s'\n",
					optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'j':
			num_jobs = strtol(optarg, NULL, 0);
			break;
		case 'Q':
			max_backlog = strtol(optarg, NULL, 0);
			break;
		case 'X':
			comp_extra = optarg;
			break;
		case 'd':
			fs_defaults = optarg;
			break;
		case 'x':
			no_xattr = true;
			break;
		case 'k':
			keep_time = true;
			break;
		case 's':
			dont_skip = true;
			break;
		case 'e':
			exportable = true;
			break;
		case 'f':
			outmode |= SQFS_FILE_OPEN_OVERWRITE;
			break;
		case 'q':
			quiet = true;
			break;
		case 'h':
			printf(usagestr, SQFS_DEFAULT_BLOCK_SIZE,
			       SQFS_DEVBLK_SIZE);
			compressor_print_available();
			exit(EXIT_SUCCESS);
		case 'V':
			print_version();
			exit(EXIT_SUCCESS);
		default:
			goto fail_arg;
		}
	}

	if (num_jobs < 1)
		num_jobs = 1;

	if (max_backlog < 1)
		max_backlog = 10 * num_jobs;

	if (comp_extra != NULL && strcmp(comp_extra, "help") == 0) {
		compressor_print_help(comp_id);
		exit(EXIT_SUCCESS);
	}

	if (optind >= argc) {
		fputs("Missing argument: squashfs image\n", stderr);
		goto fail_arg;
	}

	filename = argv[optind++];

	if (optind < argc) {
		fputs("Unknown extra arguments\n", stderr);
		goto fail_arg;
	}
	return;
fail_arg:
	fputs("Try `tar2sqfs --help' for more information.\n", stderr);
	exit(EXIT_FAILURE);
}

static int write_file(tar_header_decoded_t *hdr, file_info_t *fi,
		      data_writer_t *data)
{
	const sqfs_sparse_map_t *it;
	sqfs_file_t *file;
	uint64_t sum;
	int ret;

	if (hdr->sparse != NULL) {
		for (sum = 0, it = hdr->sparse; it != NULL; it = it->next)
			sum += it->count;

		file = sqfs_get_stdin_file(sum);
		if (file == NULL) {
			perror("packing files");
			return -1;
		}

		ret = write_data_from_file_condensed(data, file, fi,
						     hdr->sparse, 0);
		file->destroy(file);
		if (ret)
			return -1;

		return skip_padding(STDIN_FILENO, hdr->record_size);
	}

	file = sqfs_get_stdin_file(fi->size);
	if (file == NULL) {
		perror("packing files");
		return -1;
	}

	ret = write_data_from_file(data, fi, file, 0);
	file->destroy(file);

	if (ret)
		return -1;

	return skip_padding(STDIN_FILENO, fi->size);
}

static int copy_xattr(fstree_t *fs, tree_node_t *node,
		      tar_header_decoded_t *hdr)
{
	tar_xattr_t *xattr;

	for (xattr = hdr->xattr; xattr != NULL; xattr = xattr->next) {
		if (!sqfs_has_xattr(xattr->key)) {
			if (dont_skip) {
				fprintf(stderr, "Cannot encode xattr key '%s' "
					"in squashfs\n", xattr->key);
				return -1;
			}

			fprintf(stderr, "WARNING: squashfs does not "
				"support xattr prefix of %s\n", xattr->key);
			continue;
		}

		if (fstree_add_xattr(fs, node, xattr->key, xattr->value))
			return -1;
	}

	return 0;
}

static int create_node_and_repack_data(tar_header_decoded_t *hdr, fstree_t *fs,
				       data_writer_t *data)
{
	tree_node_t *node;

	if (!keep_time) {
		hdr->sb.st_mtime = fs->defaults.st_mtime;
	}

	node = fstree_add_generic(fs, hdr->name, &hdr->sb, hdr->link_target);
	if (node == NULL)
		goto fail_errno;

	if (!quiet)
		printf("Packing %s\n", hdr->name);

	if (!no_xattr) {
		if (copy_xattr(fs, node, hdr))
			return -1;
	}

	if (S_ISREG(hdr->sb.st_mode)) {
		if (write_file(hdr, node->data.file, data))
			return -1;
	}

	return 0;
fail_errno:
	perror(hdr->name);
	return -1;
}

static int process_tar_ball(fstree_t *fs, data_writer_t *data)
{
	tar_header_decoded_t hdr;
	uint64_t offset, count;
	sqfs_sparse_map_t *m;
	bool skip;
	int ret;

	for (;;) {
		ret = read_header(STDIN_FILENO, &hdr);
		if (ret > 0)
			break;
		if (ret < 0)
			return -1;

		skip = false;

		if (hdr.name == NULL || canonicalize_name(hdr.name) != 0) {
			fprintf(stderr, "skipping '%s' (invalid name)\n",
				hdr.name);
			skip = true;
		}

		if (!skip && hdr.unknown_record) {
			fprintf(stderr, "%s: unknown entry type\n", hdr.name);
			skip = true;
		}

		if (!skip && hdr.sparse != NULL) {
			offset = hdr.sparse->offset;
			count = 0;

			for (m = hdr.sparse; m != NULL; m = m->next) {
				if (m->offset < offset) {
					skip = true;
					break;
				}
				offset = m->offset + m->count;
				count += m->count;
			}

			if (count != hdr.record_size)
				skip = true;

			if (skip) {
				fprintf(stderr, "%s: broken sparse "
					"file layout)\n", hdr.name);
			}
		}

		if (skip) {
			if (dont_skip)
				goto fail;
			if (skip_entry(STDIN_FILENO, hdr.sb.st_size))
				goto fail;
			continue;
		}

		if (create_node_and_repack_data(&hdr, fs, data))
			goto fail;

		clear_header(&hdr);
	}

	return 0;
fail:
	clear_header(&hdr);
	return -1;
}

int main(int argc, char **argv)
{
	sqfs_compressor_config_t cfg;
	int status = EXIT_SUCCESS;
	sqfs_compressor_t *cmp;
	sqfs_id_table_t *idtbl;
	sqfs_file_t *outfile;
	data_writer_t *data;
	sqfs_super_t super;
	fstree_t fs;
	int ret;

	process_args(argc, argv);

	if (compressor_cfg_init_options(&cfg, comp_id,
					block_size, comp_extra)) {
		return EXIT_FAILURE;
	}

	outfile = sqfs_open_file(filename, outmode);
	if (outfile == NULL) {
		perror(filename);
		return EXIT_FAILURE;
	}

	if (fstree_init(&fs, block_size, fs_defaults))
		goto out_fd;

	cmp = sqfs_compressor_create(&cfg);
	if (cmp == NULL) {
		fputs("Error creating compressor\n", stderr);
		goto out_fs;
	}

	if (sqfs_super_init(&super, block_size, fs.defaults.st_mtime, comp_id))
		goto out_cmp;

	if (sqfs_super_write(&super, outfile))
		goto out_cmp;

	ret = cmp->write_options(cmp, outfile);
	if (ret < 0)
		goto out_cmp;

	if (ret > 0)
		super.flags |= SQFS_FLAG_COMPRESSOR_OPTIONS;

	data = data_writer_create(&super, cmp, outfile, devblksize,
				  num_jobs, max_backlog);
	if (data == NULL)
		goto out_cmp;

	idtbl = sqfs_id_table_create();
	if (idtbl == NULL)
		goto out_data;

	if (process_tar_ball(&fs, data))
		goto out;

	if (data_writer_sync(data))
		goto out;

	tree_node_sort_recursive(fs.root);
	if (fstree_gen_inode_table(&fs))
		goto out;

	super.inode_count = fs.inode_tbl_size - 2;

	fstree_xattr_deduplicate(&fs);

	if (sqfs_serialize_fstree(outfile, &super, &fs, cmp, idtbl))
		goto out;

	if (data_writer_write_fragment_table(data))
		goto out;

	if (exportable) {
		if (write_export_table(outfile, &fs, &super, cmp))
			goto out;
	}

	if (sqfs_id_table_write(idtbl, outfile, &super, cmp))
		goto out;

	if (write_xattr(outfile, &fs, &super, cmp))
		goto out;

	super.bytes_used = outfile->get_size(outfile);

	if (sqfs_super_write(&super, outfile))
		goto out;

	if (padd_sqfs(outfile, super.bytes_used, devblksize))
		goto out;

	if (!quiet) {
		fstree_gen_file_list(&fs);
		sqfs_print_statistics(&super, data_writer_get_stats(data));
	}

	status = EXIT_SUCCESS;
out:
	sqfs_id_table_destroy(idtbl);
out_data:
	data_writer_destroy(data);
out_cmp:
	cmp->destroy(cmp);
out_fs:
	fstree_cleanup(&fs);
out_fd:
	outfile->destroy(outfile);
	return status;
}
