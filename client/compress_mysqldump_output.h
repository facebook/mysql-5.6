/*
   Copyright (c) 2018, Facebook, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#ifndef CLIENT_COMPRESS_MYSQLDUMP_OUTPUT_H_
#define CLIENT_COMPRESS_MYSQLDUMP_OUTPUT_H_

struct compress_context;
struct compress_context *start_pipe_and_compress_output(
    const char *filename,
    unsigned int chunk_size,
    const char *tablename);
void finish_pipe_and_compress_output(struct compress_context *ctx);

#endif /* CLIENT_COMPRESS_MYSQLDUMP_OUTPUT_H_ */
