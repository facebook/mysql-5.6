/******************************************************
Copyright (c) 2012 Percona Ireland Ltd.

buffer datasink for XtraBackup.

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

#ifndef DS_BUFFER_H
#define DS_BUFFER_H

#include "datasink.h"

extern datasink_t datasink_buffer;

/* Change the default buffer size */
void ds_buffer_set_size(ds_ctxt_t *ctxt, size_t size);

#endif
