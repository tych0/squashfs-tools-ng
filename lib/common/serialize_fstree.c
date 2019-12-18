/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * serialize_fstree.c
 *
 * Copyright (C) 2019 David Oberhollenzer <goliath@infraroot.at>
 */
#include "common.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static sqfs_inode_generic_t *tree_node_to_inode(tree_node_t *node)
{
	sqfs_inode_generic_t *inode;
	size_t extra = 0;

	if (S_ISLNK(node->mode))
		extra = strlen(node->data.target);

	inode = calloc(1, sizeof(*inode) + extra);
	if (inode == NULL) {
		perror("creating inode");
		return NULL;
	}

	switch (node->mode & S_IFMT) {
	case S_IFSOCK:
		inode->base.type = SQFS_INODE_SOCKET;
		inode->data.ipc.nlink = node->link_count;
		break;
	case S_IFIFO:
		inode->base.type = SQFS_INODE_FIFO;
		inode->data.ipc.nlink = node->link_count;
		break;
	case S_IFLNK:
		inode->base.type = SQFS_INODE_SLINK;
		inode->data.slink.nlink = node->link_count;
		inode->data.slink.target_size = extra;
		inode->slink_target = (char *)inode->extra;
		memcpy(inode->extra, node->data.target, extra);
		break;
	case S_IFBLK:
		inode->base.type = SQFS_INODE_BDEV;
		inode->data.dev.nlink = node->link_count;
		inode->data.dev.devno = node->data.devno;
		break;
	case S_IFCHR:
		inode->base.type = SQFS_INODE_CDEV;
		inode->data.dev.nlink = node->link_count;
		inode->data.dev.devno = node->data.devno;
		break;
	default:
		assert(0);
	}

	return inode;
}

static sqfs_inode_generic_t *write_dir_entries(const char *filename,
					       sqfs_dir_writer_t *dirw,
					       tree_node_t *node)
{
	sqfs_u32 xattr, parent_inode;
	sqfs_inode_generic_t *inode;
	tree_node_t *it;
	int ret;

	ret = sqfs_dir_writer_begin(dirw, 0);
	if (ret)
		goto fail;

	for (it = node->data.dir.children; it != NULL; it = it->next) {
		ret = sqfs_dir_writer_add_entry(dirw, it->name, it->inode_num,
						it->inode_ref, it->mode);
		if (ret)
			goto fail;
	}

	ret = sqfs_dir_writer_end(dirw);
	if (ret)
		goto fail;

	xattr = node->xattr_idx;
	parent_inode = (node->parent == NULL) ? 0 : node->parent->inode_num;

	inode = sqfs_dir_writer_create_inode(dirw, 0, xattr, parent_inode);
	if (inode == NULL) {
		ret = SQFS_ERROR_ALLOC;
		goto fail;
	}

	if (inode->base.type == SQFS_INODE_DIR) {
		inode->data.dir.nlink = node->link_count;
	} else {
		inode->data.dir_ext.nlink = node->link_count;
	}

	return inode;
fail:
	sqfs_perror(filename, "recoding directory entries", ret);
	return NULL;
}

static int serialize_tree_node(const char *filename, sqfs_writer_t *wr,
			       tree_node_t *n)
{
	sqfs_inode_generic_t *inode;
	sqfs_u32 offset;
	sqfs_u64 block;
	int ret;

	if (S_ISDIR(n->mode)) {
		inode = write_dir_entries(filename, wr->dirwr, n);
		ret = SQFS_ERROR_INTERNAL;
	} else if (S_ISREG(n->mode)) {
		inode = n->data.file.user_ptr;
		n->data.file.user_ptr = NULL;
		ret = SQFS_ERROR_INTERNAL;
	} else {
		inode = tree_node_to_inode(n);
		ret = SQFS_ERROR_ALLOC;
	}

	if (inode == NULL)
		return ret;

	inode->base.mode = n->mode;
	inode->base.mod_time = n->mod_time;
	inode->base.inode_number = n->inode_num;

	sqfs_inode_set_xattr_index(inode, n->xattr_idx);

	ret = sqfs_id_table_id_to_index(wr->idtbl, n->uid,
					&inode->base.uid_idx);
	if (ret)
		goto out;

	ret = sqfs_id_table_id_to_index(wr->idtbl, n->gid,
					&inode->base.gid_idx);
	if (ret)
		goto out;

	sqfs_meta_writer_get_position(wr->im, &block, &offset);
	n->inode_ref = (block << 16) | offset;

	ret = sqfs_meta_writer_write_inode(wr->im, inode);
out:
	free(inode);
	return ret;
}

static int serialize_recursive(const char *filename, sqfs_writer_t *wr,
			       tree_node_t *root)
{
	bool has_subdirs = false;
	tree_node_t *it;
	int ret;

	for (it = root->data.dir.children; it != NULL; it = it->next) {
		if (S_ISDIR(it->mode)) {
			has_subdirs = true;
			break;
		}
	}

	if (has_subdirs) {
		for (it = root->data.dir.children; it != NULL; it = it->next) {
			if (S_ISDIR(it->mode)) {
				ret = serialize_recursive(filename, wr, it);
				if (ret)
					return ret;
			}
		}
	}

	for (it = root->data.dir.children; it != NULL; it = it->next) {
		ret = serialize_tree_node(filename, wr, it);
		if (ret)
			return ret;
	}

	return 0;
}

int sqfs_serialize_fstree(const char *filename, sqfs_writer_t *wr)
{
	int ret;

	wr->super.inode_table_start = wr->outfile->get_size(wr->outfile);

	if (S_ISDIR(wr->fs.root->mode)) {
		ret = serialize_recursive(filename, wr, wr->fs.root);
		if (ret)
			goto out;
	}

	ret = serialize_tree_node(filename, wr, wr->fs.root);
	if (ret)
		goto out;

	ret = sqfs_meta_writer_flush(wr->im);
	if (ret)
		goto out;

	ret = sqfs_meta_writer_flush(wr->dm);
	if (ret)
		goto out;

	wr->super.root_inode_ref = wr->fs.root->inode_ref;
	wr->super.directory_table_start = wr->outfile->get_size(wr->outfile);

	ret = sqfs_meta_write_write_to_file(wr->dm);
	if (ret)
		goto out;

	ret = 0;
out:
	if (ret)
		sqfs_perror(filename, "storing filesystem tree", ret);
	return ret;
}
