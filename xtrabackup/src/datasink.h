/******************************************************
Copyright (c) 2011 Percona Ireland Ltd.

Data sink interface.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

*******************************************************/

#ifndef XB_DATASINK_H
#define XB_DATASINK_H

#include <my_global.h>
#include <my_dir.h>

struct datasink_struct;

typedef struct ds_ctxt {
	struct datasink_struct	*datasink;
	char 			*root;
	void			*ptr;
	struct ds_ctxt		*pipe_ctxt;
} ds_ctxt_t;

typedef struct {
	void 			*ptr;
	char 			*path;
	struct datasink_struct	*datasink;
} ds_file_t;

typedef struct datasink_struct {
	ds_ctxt_t *(*init)(const char *root);
	ds_file_t *(*open)(ds_ctxt_t *ctxt, const char *path, MY_STAT *stat);
	int (*write)(ds_file_t *file, const void *buf, size_t len);
	int (*close)(ds_file_t *file);
	void (*deinit)(ds_ctxt_t *ctxt);
} datasink_t;

#endif /* XB_DATASINK_H */
