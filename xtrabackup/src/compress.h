/******************************************************
Copyright (c) 2011 Percona Ireland Ltd.

Compression interface for XtraBackup.

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

#ifndef XB_COMPRESS_H
#define XB_COMPRESS_H

#include "datasink.h"

extern datasink_t datasink_compress;

#ifdef __cplusplus
extern "C" {
#endif

/* Return a target datasink for the specified compress datasink */
ds_ctxt_t *compress_get_dest_ctxt(ds_ctxt_t *);

#ifdef __cplusplus
}
#endif

#endif
