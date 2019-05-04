/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "id_table.h"
#include "table.h"

int id_table_write(id_table_t *tbl, int outfd, sqfs_super_t *super,
		   compressor_t *cmp)
{
	size_t i;
	int ret;

	for (i = 0; i < tbl->num_ids; ++i)
		tbl->ids[i] = htole32(tbl->ids[i]);

	super->id_count = tbl->num_ids;

	ret = sqfs_write_table(outfd, super, tbl->ids, sizeof(tbl->ids[0]),
			       tbl->num_ids, &super->id_table_start, cmp);

	for (i = 0; i < tbl->num_ids; ++i)
		tbl->ids[i] = le32toh(tbl->ids[i]);

	return ret;
}