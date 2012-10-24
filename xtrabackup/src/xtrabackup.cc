/******************************************************
XtraBackup: hot backup tool for InnoDB
(c) 2009-2012 Percona Ireland Ltd
Originally Created 3/3/2009 Yasufumi Kinoshita
Written by Alexey Kopytov, Aleksandr Kuzminsky, Stewart Smith, Vadim Tkachenko,
Yasufumi Kinoshita, Ignacio Nin and Baron Schwartz.

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

*******************************************************

This file incorporates work covered by the following copyright and
permission notice:

Copyright (c) 2000, 2011, MySQL AB & Innobase Oy. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place, Suite 330, Boston, MA 02111-1307 USA

*******************************************************/

#ifndef XTRABACKUP_VERSION
#define XTRABACKUP_VERSION "undefined"
#endif
#ifndef XTRABACKUP_REVISION
#define XTRABACKUP_REVISION "undefined"
#endif

//#define XTRABACKUP_TARGET_IS_PLUGIN

#include <mysql_version.h>
#include <my_base.h>
#include <my_getopt.h>
#include <mysql_com.h>
#if MYSQL_VERSION_ID >= 50600
#include <my_default.h>
#endif

#if (MYSQL_VERSION_ID < 50100)
#define G_PTR gptr
#else /* MYSQL_VERSION_ID < 51000 */
#define G_PTR uchar*
#endif

#if (MYSQL_VERSION_ID < 50600)
extern "C" {
#endif

#include <univ.i>
#include <os0file.h>
#include <os0sync.h>
#include <os0thread.h>
#include <srv0start.h>
#include <srv0srv.h>
#include <trx0roll.h>
#include <trx0trx.h>
#include <trx0sys.h>
#include <mtr0mtr.h>
#include <row0ins.h>
#include <row0mysql.h>
#include <row0sel.h>
#include <row0upd.h>
#include <log0log.h>
#include <log0recv.h>
#include <lock0lock.h>
#include <dict0crea.h>
#if MYSQL_VERSION_ID >= 50600
#include <dict0priv.h>
#include <dict0stats.h>
#include <ut0crc32.h>
#include <mysqld.h>
#endif
#include <btr0cur.h>
#include <btr0btr.h>
#include <btr0sea.h>
#include <fsp0fsp.h>
#include <sync0sync.h>
#include <fil0fil.h>
#include <trx0xa.h>
#include <btr0sea.h>
#include <log0recv.h>
#include <fcntl.h>
#include <buf0flu.h>
#include <buf0lru.h>
#include <sql_locale.h>
#include <derror.h>

#ifdef INNODB_VERSION_SHORT
#include <ibuf0ibuf.h>
#include <page0zip.h>
#endif

#if (MYSQL_VERSION_ID < 50600)
} /* extern "C" */
#endif

#include "common.h"
#include "datasink.h"
#include "local.h"
#include "stream.h"
#include "compress.h"

#include "xb_regex.h"

#ifndef INNODB_VERSION_SHORT
/* MySQL 5.1 built-in only */
#define IB_INT64 ib_longlong
#define IB_UINT64 ib_ulonglong
typedef dulint	lsn_t;
#ifdef LSN_PF
#undef LSN_PF /* Any LSN_PF use should be error for the builtin */
#endif
#define MACH_READ_64 mach_read_from_8
#define MACH_WRITE_64 mach_write_to_8
#define OS_MUTEX_CREATE() os_mutex_create(NULL)
#define PAGE_ZIP_MIN_SIZE_SHIFT	10
#define DICT_TF_ZSSIZE_SHIFT	1
#define DICT_TF_FORMAT_ZIP	1
#define DICT_TF_FORMAT_SHIFT		5
#define xb_buf_page_is_corrupted(page, zip_size)	\
	buf_page_is_corrupted(page)
#define dict_stats_update_transient(table) \
	dict_update_statistics_low(table, TRUE)
#else /* INNODB_VERSION_SHORT */
#ifdef XTRADB_BASED
/* Any XtraDB version */
#define dict_stats_update_transient(table) \
	dict_update_statistics(table, TRUE, FALSE)
#endif /* XTRADB_BASED */
/* MySQL 5.1 plugin+ */
#define IB_INT64 ib_int64_t
#define IB_UINT64 ib_uint64_t
#if (MYSQL_VERSION_ID < 50500)
/* MySQL 5.1 plugin only */
#define MACH_READ_64 mach_read_ull
#define MACH_WRITE_64 mach_write_ull
#define OS_MUTEX_CREATE() os_mutex_create(NULL)
#else /* MYSQL_VERSION_ID < 50500 */
/* MySQL 5.5+ */
#define MACH_READ_64 mach_read_from_8
#define MACH_WRITE_64 mach_write_to_8
#define OS_MUTEX_CREATE() os_mutex_create()
#if MYSQL_VERSION_ID >= 50600
/* MySQL 5.6+ */
#define PAGE_ZIP_MIN_SIZE_SHIFT	10
#define DICT_TF_ZSSIZE_SHIFT	1
#define DICT_TF_FORMAT_ZIP	1
#define DICT_TF_FORMAT_SHIFT		5
#define xb_os_file_read(file, buf, offset, n)				\
	pfs_os_file_read_func(file, buf, offset, n, __FILE__, __LINE__)
#define xb_os_file_write(name, file, buf, offset, n) \
	pfs_os_file_write_func(name, file, buf, offset,	\
			       n, __FILE__, __LINE__)
#define xb_buf_page_is_corrupted(page, zip_size)	\
	buf_page_is_corrupted(TRUE, page, zip_size)
#define xb_btr_pcur_open_at_index_side(from_left, index, latch_mode, pcur, \
				       init_pcur, level, mtr)		\
	btr_pcur_open_at_index_side(from_left, index, latch_mode, pcur,	\
				    init_pcur, 0, mtr)
#define xb_os_file_set_size(name, file, size) \
	os_file_set_size(name, file, size)
#define xb_fil_rename_tablespace(old_name_in, id, new_name, new_path) \
	fil_rename_tablespace(old_name_in, id, new_name, new_path)
#define xb_btr_root_block_get(index, mode, mtr) \
	btr_root_block_get(index, mode, mtr)
typedef ib_mutex_t	mutex_t;
#define INNODB_LOG_DIR srv_log_group_home_dir
#define DEFAULT_LOG_FILE_SIZE 48*1024*1024
/* InnoDB data dictionary API in MySQL 5.5- works on on tables named
"./database/table.ibd", and in 5.6+ on "database/table".  This variable handles
the presence or absence of "./".  */
static const char* xb_dict_prefix = "";
/* And this one handles truncating or leaving the final ".ibd". */
#define XB_DICT_SUFFIX_LEN 4
#define xb_os_event_create(name) os_event_create()
#else /* MYSQL_VERSION_ID >= 50600 */
/* MySQL 5.5 and Percona Server 5.5 */
#define xb_os_file_write(name, file, buf, offset, n)		\
	pfs_os_file_write_func(name, file, buf,				\
			       (ulint)(offset & 0xFFFFFFFFUL),		\
			       (ulint)(((ullint)offset) >> 32),		\
			       n, __FILE__, __LINE__)
#ifdef XTRADB_BASED
/* Percona Server 5.5 only */
#define xb_os_file_read(file, buf, offset, n)			\
	pfs_os_file_read_func(file, buf,			\
				      (ulint)(offset & 0xFFFFFFFFUL),	\
				      (ulint)(((ullint)offset) >> 32),	\
				      n,  NULL, __FILE__, __LINE__)
#else /* XTRADB_BASED */
/* MySQL 5.5 only */
#define xb_os_file_read(file, buf, offset, n)			\
	pfs_os_file_read_func(file, buf,			\
			      (ulint)(offset & 0xFFFFFFFFUL),	\
			      (ulint)(((ullint)offset) >> 32),	\
			      n,  __FILE__, __LINE__)
#endif /* XTRADB_BASED */
#endif /* MYSQL_VERSION_ID >= 50600 */
#endif /* MYSQL_VERSION_ID < 50500 */
/* MySQL 5.1 plugin+ */
#define ut_dulint_zero 0
#define ut_dulint_cmp(A, B) (A > B ? 1 : (A == B ? 0 : -1))
#define ut_dulint_add(A, B) (A + B)
#define ut_dulint_minus(A, B) (A - B)
#define ut_dulint_align_down(A, B) (A & ~((ib_int64_t)B - 1))
#define ut_dulint_align_up(A, B) ((A + B - 1) & ~((ib_int64_t)B - 1))
#endif /* INODB_VERSION_SHORT */

#if MYSQL_VERSION_ID < 50500
/* MySQL 5.1 builtin and plugin only */
#define xb_os_file_write(name, file, buf, offset, n)			\
	os_file_write(name, file, buf,					\
		      (ulint)(offset & 0xFFFFFFFFUL),			\
		      (ulint)(((ullint)offset) >> 32),			\
		      n)
#define xb_os_file_read(file, buf, offset, n)				\
	os_file_read(file, buf,						\
		     (ulint)(offset & 0xFFFFFFFFUL),			\
		     (ulint)(((ullint)offset) >> 32),			\
		     n)
#endif

#if MYSQL_VERSION_ID < 50600 && defined(INNODB_VERSION_SHORT)
/* MySQL 5.1 plugin and MySQL 5.5 only */
typedef ib_uint64_t	lsn_t;
#define LSN_PF		"%llu"
#define xb_buf_page_is_corrupted(page, zip_size)	\
	buf_page_is_corrupted(page, zip_size)
#endif /* MYSQL_VERSION_ID < 50600 && defined(INNODB_VERSION_SHORT) */

#if MYSQL_VERSION_ID < 50600
#if !defined(XTRADB_BASED) && defined (INNODB_VERSION_SHORT)
/* MySQL 5.1 plugin - MySQL 5.5, Oracle version */
#define dict_stats_update_transient(table)	\
	dict_update_statistics(table, TRUE)
#endif /* !defined(XTRADB_BASED) && defined (INNODB_SHORT_VERSION) */
/* MySQL 5.1 builtin - MySQL 5.5 only  */
#define xb_os_event_create(name) os_event_create(name)
static const char* xb_dict_prefix = "./";
#define XB_DICT_SUFFIX_LEN 0
#define INT64PF	"%lld"
#define UINT64PF "%llu"
#define DEFAULT_LOG_FILE_SIZE 5*1024*1024
typedef ulint	dberr_t;
typedef os_mutex_t	os_ib_mutex_t;
#define xb_btr_pcur_open_at_index_side(from_left, index, latch_mode, pcur, \
				       init_pcur, level, mtr)		\
	btr_pcur_open_at_index_side(from_left, index, latch_mode, pcur,	\
				    init_pcur, mtr)
#define xb_fil_rename_tablespace(old_name_in, id, new_name, new_path)	\
	fil_rename_tablespace(old_name_in, id, new_name)
#define xb_btr_root_block_get(index, mode, mtr) \
	btr_root_block_get(index, mtr)
#define UNIV_FORMAT_MAX DICT_TF_FORMAT_51;
#define dict_tf_get_zip_size dict_table_flags_to_zip_size
#define os_file_get_size(file) os_file_get_size_as_iblonglong(file)
#define xb_os_file_set_size(name, file, size)		\
	os_file_set_size(name, file, (ulong)(size & 0xFFFFFFFFUL),	\
			 (ulong)((ullint)size >> 32))
#define fsp_flags_is_compressed(flags) ((flags) & DICT_TF_ZSSIZE_MASK)
#define fsp_flags_get_zip_size(flags)					\
	((PAGE_ZIP_MIN_SIZE >> 1) << (((flags) & DICT_TF_ZSSIZE_MASK)	\
				      >> DICT_TF_ZSSIZE_SHIFT))

#define LOG_CHECKPOINT_OFFSET_LOW32	LOG_CHECKPOINT_OFFSET
#define INNODB_LOG_DIR innobase_log_group_home_dir
#endif

#ifdef __WIN__
#define SRV_PATH_SEPARATOR	'\\'
#define SRV_PATH_SEPARATOR_STR	"\\"	
#else
#define SRV_PATH_SEPARATOR	'/'
#define SRV_PATH_SEPARATOR_STR	"/"
#endif

#ifndef UNIV_PAGE_SIZE_MAX
#define UNIV_PAGE_SIZE_MAX UNIV_PAGE_SIZE
#endif
#ifndef UNIV_PAGE_SIZE_SHIFT_MAX
#define UNIV_PAGE_SIZE_SHIFT_MAX UNIV_PAGE_SIZE_SHIFT
#endif

#if (MYSQL_VERSION_ID >= 50507) && (MYSQL_VERSION_ID < 50600)
/*
   As of MySQL 5.5.7, InnoDB uses thd_wait plugin service.
   We have to provide mock functions to avoid linker errors.
*/
#include <mysql/plugin.h>
#include <mysql/service_thd_wait.h>

extern "C" {

void thd_wait_begin(MYSQL_THD thd, int wait_type)
{
	(void)thd;
	(void)wait_type;
	return;
}

void thd_wait_end(MYSQL_THD thd)
{
	(void)thd;
	return;
}

}

#endif /* (MYSQL_VERSION_ID >= 50507) && (MYSQL_VERSION_ID < 50600) */

/* prototypes for static and non-prototyped functions in original */
#ifndef INNODB_VERSION_SHORT

extern "C" {

page_t*
btr_node_ptr_get_child(
/*===================*/
				/* out: child page, x-latched */
	rec_t*		node_ptr,/* in: node pointer */
	const ulint*	offsets,/* in: array returned by rec_get_offsets() */
	mtr_t*		mtr);	/* in: mtr */

ibool
thd_is_replication_slave_thread(
	void*	thd);

ibool
thd_has_edited_nontrans_tables(
	void*	thd);

ibool
thd_is_select(
	const void*	thd);

void
innobase_mysql_print_thd(
	FILE*   f,
	void*   input_thd,
	uint	max_query_len);

void
innobase_convert_from_table_id(
	char*	to,
	const char*	from,
	ulint	len);

void
innobase_convert_from_id(
	char*	to,
	const char*	from,
	ulint	len);

int
innobase_strcasecmp(
	const char*	a,
	const char*	b);

void
innobase_casedn_str(
	char*	a);

struct charset_info_st*
innobase_get_charset(
	void*   mysql_thd);

int
innobase_mysql_tmpfile(void);

void
innobase_invalidate_query_cache(
	trx_t*	trx,
	char*	full_name,
	ulint	full_name_len);

/*****************************************************************//**
Convert a table or index name to the MySQL system_charset_info (UTF-8)
and quote it if needed.
@return	pointer to the end of buf */
char*
innobase_convert_name(
/*==================*/
	char*		buf,	/*!< out: buffer for converted identifier */
	ulint		buflen,	/*!< in: length of buf, in bytes */
	const char*	id,	/*!< in: identifier to convert */
	ulint		idlen,	/*!< in: length of id, in bytes */
	void*		thd,	/*!< in: MySQL connection thread, or NULL */
	ibool		table_id);/*!< in: TRUE=id is a table or database name;
					   FALSE=id is an index name */

int
innobase_mysql_cmp(
	int		mysql_type,
	uint		charset_number,
	unsigned char*	a,
	unsigned int	a_length,
	unsigned char*	b,
	unsigned int	b_length);

ulint
innobase_get_at_most_n_mbchars(
	ulint charset_id,
	ulint prefix_len,
	ulint data_len,
	const char* str);

} /* extern "C" */

#else /* #ifndef INNODB_VERSION_SHORT */

#if MYSQL_VERSION_ID < 50600
extern "C" {
#endif

buf_block_t*
btr_node_ptr_get_child(
/*===================*/
	const rec_t*	node_ptr,/*!< in: node pointer */
	dict_index_t*	index,	/*!< in: index */
	const ulint*	offsets,/*!< in: array returned by rec_get_offsets() */
	mtr_t*		mtr);	/*!< in: mtr */

#if MYSQL_VERSION_ID < 50600
}
#endif

#if MYSQL_VERSION_ID >= 50600

buf_block_t*
btr_root_block_get(
/*===============*/
	const dict_index_t*	index,	/*!< in: index tree */
	ulint			mode,	/*!< in: either RW_S_LATCH
					  or RW_X_LATCH */
	mtr_t*			mtr);	/*!< in: mtr */

int
fil_file_readdir_next_file(
/*=======================*/
	dberr_t*	err,	/*!< out: this is set to DB_ERROR if an error
				was encountered, otherwise not changed */
	const char*	dirname,/*!< in: directory name or path */
	os_file_dir_t	dir,	/*!< in: directory stream */
	os_file_stat_t*	info);	/*!< in/out: buffer where the
				info is returned */

ibool
recv_check_cp_is_consistent(
/*========================*/
	const byte*	buf);	/*!< in: buffer containing checkpoint info */

dberr_t
open_or_create_data_files(
/*======================*/
	ibool*		create_new_db,	/*!< out: TRUE if new database should be
					created */
	lsn_t*		min_flushed_lsn,/*!< out: min of flushed lsn
					values in data files */
	lsn_t*		max_flushed_lsn,/*!< out: max of flushed lsn
					values in data files */
	ulint*		sum_of_new_sizes);/*!< out: sum of sizes of the
					new files added */

#else /* MYSQL_VERSION_ID >= 50600 */

extern "C" {

buf_block_t*
btr_root_block_get(
/*===============*/
	dict_index_t*	index,	/*!< in: index tree */
	mtr_t*		mtr);	/*!< in: mtr */

int
innobase_mysql_cmp(
/*===============*/
	int		mysql_type,
	uint		charset_number,
	unsigned char*	a,
	unsigned int	a_length,
	unsigned char*	b,
	unsigned int	b_length);

} /* extern "C" */

#endif /* MYSQL_VERSION_ID >= 50600 */

#endif /* #ifndef INNODB_VERSION_SHORT */

#if MYSQL_VERSION_ID < 50600
extern "C" {
#endif
	
#ifdef XTRADB_BASED
trx_t*
innobase_get_trx();
#endif

void
innobase_get_cset_width(
	ulint	cset,
	ulint*	mbminlen,
	ulint*	mbmaxlen);

int
fil_file_readdir_next_file(
/*=======================*/
				/* out: 0 if ok, -1 if error even after the
				retries, 1 if at the end of the directory */
	ulint*		err,	/* out: this is set to DB_ERROR if an error
				was encountered, otherwise not changed */
	const char*	dirname,/* in: directory name or path */
	os_file_dir_t	dir,	/* in: directory stream */
	os_file_stat_t*	info);	/* in/out: buffer where the info is returned */

ibool
recv_check_cp_is_consistent(
/*========================*/
			/* out: TRUE if ok */
	byte*	buf);	/* in: buffer containing checkpoint info */

ulint
recv_find_max_checkpoint(
/*=====================*/
					/* out: error code or DB_SUCCESS */
	log_group_t**	max_group,	/* out: max group */
	ulint*		max_field);	/* out: LOG_CHECKPOINT_1 or
					LOG_CHECKPOINT_2 */

#if MYSQL_VERSION_ID < 50600
} /* extern "C" */
#endif

#if MYSQL_VERSION_ID >= 50600

ibool
log_block_checksum_is_ok_or_old_format(
/*===================================*/
	const byte*	block);	/*!< in: pointer to a log block */

#else

extern "C" {

ibool
log_block_checksum_is_ok_or_old_format(
/*===================================*/
			/* out: TRUE if ok, or if the log block may be in the
			format of InnoDB version < 3.23.52 */
	byte*	block);	/* in: pointer to a log block */

ulint
open_or_create_log_file(
/*====================*/
					/* out: DB_SUCCESS or error code */
        ibool   create_new_db,          /* in: TRUE if we should create a
                                        new database */
	ibool*	log_file_created,	/* out: TRUE if new log file
					created */
	ibool	log_file_has_been_opened,/* in: TRUE if a log file has been
					opened before: then it is an error
					to try to create another log file */
	ulint	k,			/* in: log group number */
	ulint	i);			/* in: log file number in group */

ulint
open_or_create_data_files(
/*======================*/
				/* out: DB_SUCCESS or error code */
	ibool*	create_new_db,	/* out: TRUE if new database should be
								created */
#ifdef XTRADB_BASED
	ibool*	create_new_doublewrite_file,
#endif 
#ifdef UNIV_LOG_ARCHIVE
	ulint*	min_arch_log_no,/* out: min of archived log numbers in data
				files */
	ulint*	max_arch_log_no,/* out: */
#endif /* UNIV_LOG_ARCHIVE */
	lsn_t*	min_flushed_lsn,/* out: min of flushed lsn values in data
				files */
	lsn_t*	max_flushed_lsn,/* out: */
	ulint*	sum_of_new_sizes);/* out: sum of sizes of the new files added */

} /* extern "C" */

#endif /* MYSQL_VERSION_ID >= 50600 */

#if MYSQL_VERSION_ID < 50600
extern "C" {
#endif

void
os_file_set_nocache(
/*================*/
	int		fd,		/* in: file descriptor to alter */
	const char*	file_name,	/* in: used in the diagnostic message */
	const char*	operation_name);	/* in: used in the diagnostic message,
					we call os_file_set_nocache()
					immediately after opening or creating
					a file, so this is either "open" or
					"create" */

#if MYSQL_VERSION_ID < 50600
} /* extern "C" */
#endif

/****************************************************************//**
A simple function to open or create a file.
@return own: handle to the file, not defined if error, error number
can be retrieved with os_file_get_last_error */
UNIV_INLINE
os_file_t
xb_file_create_no_error_handling(
/*=============================*/
	const char*	name,	/*!< in: name of the file or path as a
				null-terminated string */
	ulint		create_mode,/*!< in: OS_FILE_OPEN if an existing file
				is opened (if does not exist, error), or
				OS_FILE_CREATE if a new file is created
				(if exists, error) */
	ulint		access_type,/*!< in: OS_FILE_READ_ONLY,
				OS_FILE_READ_WRITE, or
				OS_FILE_READ_ALLOW_DELETE; the last option is
				used by a backup program reading the file */
	ibool*		success);/*!< out: TRUE if succeed, FALSE if error */

/****************************************************************//**
Opens an existing file or creates a new.
@return own: handle to the file, not defined if error, error number
can be retrieved with os_file_get_last_error */
UNIV_INLINE
os_file_t
xb_file_create(
/*===========*/
	const char*	name,	/*!< in: name of the file or path as a
				null-terminated string */
	ulint		create_mode,/*!< in: OS_FILE_OPEN if an existing file
				is opened (if does not exist, error), or
				OS_FILE_CREATE if a new file is created
				(if exists, error),
				OS_FILE_OVERWRITE if a new file is created
				or an old overwritten;
				OS_FILE_OPEN_RAW, if a raw device or disk
				partition should be opened */
	ulint		purpose,/*!< in: OS_FILE_AIO, if asynchronous,
				non-buffered i/o is desired,
				OS_FILE_NORMAL, if any normal file;
				NOTE that it also depends on type, os_aio_..
				and srv_.. variables whether we really use
				async i/o or unbuffered i/o: look in the
				function source code for the exact rules */
	ulint		type,	/*!< in: OS_DATA_FILE or OS_LOG_FILE */
	ibool*		success);/*!< out: TRUE if succeed, FALSE if error */


/***********************************************************************//**
Renames a file (can also move it to another directory). It is safest that the
file is closed before calling this function.
@return	TRUE if success */
UNIV_INLINE
ibool
xb_file_rename(
/*===========*/
	const char*	oldpath,/*!< in: old file path as a null-terminated
				string */
	const char*	newpath);/*!< in: new file path */

UNIV_INLINE
void
xb_file_set_nocache(
/*================*/
	os_file_t	fd,		/* in: file descriptor to alter */
	const char*	file_name,	/* in: used in the diagnostic message */
	const char*	operation_name);/* in: used in the diagnostic message,
					we call os_file_set_nocache()
					immediately after opening or creating
					a file, so this is either "open" or
					"create" */

/***********************************************************************//**
Compatibility wrapper around os_file_flush().
@return	TRUE if success */
static
ibool
xb_file_flush(
/*==========*/
	os_file_t	file);	/*!< in, own: handle to a file */

#ifdef INNODB_VERSION_SHORT
#define XB_HASH_SEARCH(NAME, TABLE, FOLD, DATA, ASSERTION, TEST) \
	HASH_SEARCH(NAME, TABLE, FOLD, xtrabackup_tables_t*, DATA, ASSERTION, \
		    TEST)
#else
#define XB_HASH_SEARCH(NAME, TABLE, FOLD, DATA, ASSERTION, TEST) \
	HASH_SEARCH(NAME, TABLE, FOLD, DATA, TEST)
#endif

typedef struct {
	ulint	page_size;
	ulint	zip_size;
	ulint	space_id;
} xb_delta_info_t;

extern fil_system_t*   fil_system;

/** Value of fil_space_struct::magic_n */
#define	FIL_SPACE_MAGIC_N	89472

/*******************************************************************//**
Returns the table space by a given id, NULL if not found. */
fil_space_t*
xb_space_get_by_id(
/*================*/
	ulint	id);	/*!< in: space id */

/*******************************************************************//**
Returns the table space by a given name, NULL if not found. */
fil_space_t*
xb_space_get_by_name(
/*==================*/
	const char*	name);	/*!< in: space name */

/*******************************************************************//**
Returns the table space by a given id, NULL if not found. */
fil_space_t*
xb_space_get_by_id(
/*================*/
	ulint	id)	/*!< in: space id */
{
	fil_space_t*	space;

	ut_ad(mutex_own(&fil_system->mutex));

#ifdef INNODB_VERSION_SHORT
	HASH_SEARCH(hash, fil_system->spaces, id,
		    fil_space_t*, space,
		    ut_ad(space->magic_n == FIL_SPACE_MAGIC_N),
		    space->id == id);
#else
	HASH_SEARCH(hash, fil_system->spaces, id, space, space->id == id);
#endif

	return(space);
}

/*******************************************************************//**
Returns the table space by a given name, NULL if not found. */
fil_space_t*
xb_space_get_by_name(
/*==================*/
	const char*	name)	/*!< in: space name */
{
	fil_space_t*	space;
#ifdef INNODB_VERSION_SHORT
	ulint		fold;
#endif

	ut_ad(mutex_own(&fil_system->mutex));

#ifdef INNODB_VERSION_SHORT
	fold = ut_fold_string(name);
	HASH_SEARCH(name_hash, fil_system->name_hash, fold,
		    fil_space_t*, space,
		    ut_ad(space->magic_n == FIL_SPACE_MAGIC_N),
		    !strcmp(name, space->name));
#else
	HASH_SEARCH(name_hash, fil_system->name_hash, ut_fold_string(name),
		    space, 0 == strcmp(name, space->name));
#endif

	return(space);
}

#ifndef INNODB_VERSION_SHORT

/*******************************************************************//**
Free all spaces in space_list. */
static
void
fil_free_all_spaces(void)
/*=====================*/
{
	fil_space_t*	space;

	mutex_enter(&fil_system->mutex);

	space = UT_LIST_GET_FIRST(fil_system->space_list);

	while (space != NULL) {
		fil_space_t*	prev_space = space;

		space = UT_LIST_GET_NEXT(space_list, space);

		fil_space_free(prev_space->id, FALSE);
	}

	mutex_exit(&fil_system->mutex);
}

#define SRV_SHUTDOWN_NONE 0

#endif

/* ==end=== definition  at fil0fil.c === */


my_bool innodb_inited= 0;

/* === xtrabackup specific options === */
char xtrabackup_real_target_dir[FN_REFLEN] = "./xtrabackup_backupfiles/";
char *xtrabackup_target_dir= xtrabackup_real_target_dir;
my_bool xtrabackup_version = FALSE;
my_bool xtrabackup_backup = FALSE;
my_bool xtrabackup_stats = FALSE;
my_bool xtrabackup_prepare = FALSE;
my_bool xtrabackup_print_param = FALSE;

my_bool xtrabackup_export = FALSE;
my_bool xtrabackup_apply_log_only = FALSE;

my_bool xtrabackup_suspend_at_end = FALSE;
longlong xtrabackup_use_memory = 100*1024*1024L;
my_bool xtrabackup_create_ib_logfile = FALSE;

long xtrabackup_throttle = 0; /* 0:unlimited */
lint io_ticket;
os_event_t wait_throttle = NULL;

my_bool xtrabackup_log_only = FALSE;
char *xtrabackup_incremental = NULL;
lsn_t incremental_lsn;
lsn_t incremental_to_lsn;
lsn_t incremental_last_lsn;

char *xtrabackup_incremental_basedir = NULL; /* for --backup */
char *xtrabackup_extra_lsndir = NULL; /* for --backup with --extra-lsndir */
char *xtrabackup_incremental_dir = NULL; /* for --prepare */

char *xtrabackup_tables = NULL;
int tables_regex_num;
xb_regex_t *tables_regex;
xb_regmatch_t tables_regmatch[1];

char *xtrabackup_tables_file = NULL;
hash_table_t* tables_hash;

struct xtrabackup_tables_struct{
	char*		name;
	hash_node_t	name_hash;
};
typedef struct xtrabackup_tables_struct	xtrabackup_tables_t;

#ifdef XTRADB_BASED
static ulint		thread_nr[SRV_MAX_N_IO_THREADS + 6 + 64];
static os_thread_id_t	thread_ids[SRV_MAX_N_IO_THREADS + 6 + 64];
#else
static ulint		thread_nr[SRV_MAX_N_IO_THREADS + 6];
static os_thread_id_t	thread_ids[SRV_MAX_N_IO_THREADS + 6];
#endif

lsn_t checkpoint_lsn_start;
lsn_t checkpoint_no_start;
lsn_t log_copy_scanned_lsn;
ibool log_copying = TRUE;
ibool log_copying_running = FALSE;
ibool log_copying_succeed = FALSE;

ibool xtrabackup_logfile_is_renamed = FALSE;

int xtrabackup_parallel;

char *xtrabackup_stream_str = NULL;
xb_stream_fmt_t xtrabackup_stream_fmt;
my_bool xtrabackup_stream = FALSE;

static const char *xtrabackup_compress_alg = NULL;
ibool xtrabackup_compress = FALSE;
uint xtrabackup_compress_threads;

/* === metadata of backup === */
#define XTRABACKUP_METADATA_FILENAME "xtrabackup_checkpoints"
char metadata_type[30] = ""; /*[full-backuped|full-prepared|incremental]*/
#ifndef INNODB_VERSION_SHORT
lsn_t metadata_from_lsn = {0, 0};
lsn_t metadata_to_lsn = {0, 0};
lsn_t metadata_last_lsn = {0, 0};
#else
lsn_t metadata_from_lsn = 0;
lsn_t metadata_to_lsn = 0;
lsn_t metadata_last_lsn = 0;
#endif

#define XB_DELTA_INFO_SUFFIX ".meta"

#define XB_LOG_FILENAME "xtrabackup_logfile"

#ifdef __WIN__
#define XB_FILE_UNDEFINED NULL
#else
#define XB_FILE_UNDEFINED (-1)
#endif

int	dst_log_fd = XB_FILE_UNDEFINED;
char	dst_log_path[FN_REFLEN];

/* === some variables from mysqld === */
#if MYSQL_VERSION_ID < 50600
char mysql_real_data_home[FN_REFLEN] = "./";
char *opt_mysql_tmpdir = NULL;
MY_TMPDIR mysql_tmpdir_list;
char *mysql_data_home= mysql_real_data_home;
#endif
static char mysql_data_home_buff[2];


const char *defaults_group = "mysqld";

/* === static parameters in ha_innodb.cc */

#define HA_INNOBASE_ROWS_IN_TABLE 10000 /* to get optimization right */
#define HA_INNOBASE_RANGE_COUNT	  100

ulong 	innobase_large_page_size = 0;

/* The default values for the following, type long or longlong, start-up
parameters are declared in mysqld.cc: */

long innobase_additional_mem_pool_size = 1*1024*1024L;
long innobase_buffer_pool_awe_mem_mb = 0;
long innobase_file_io_threads = 4;
long innobase_read_io_threads = 4;
long innobase_write_io_threads = 4;
long innobase_force_recovery = 0;
long innobase_lock_wait_timeout = 50;
long innobase_log_buffer_size = 1024*1024L;
long innobase_log_files_in_group = 2;
long innobase_log_files_in_group_backup;
long innobase_mirrored_log_groups = 1;
long innobase_open_files = 300L;

long innobase_page_size = (1 << 14); /* 16KB */
#ifdef XTRADB_BASED
static ulong innobase_log_block_size = 512;
#endif
my_bool innobase_fast_checksum = FALSE;
my_bool	innobase_extra_undoslots = FALSE;
char*	innobase_doublewrite_file = NULL;

longlong innobase_buffer_pool_size = 8*1024*1024L;
longlong innobase_log_file_size = DEFAULT_LOG_FILE_SIZE;
longlong innobase_log_file_size_backup;

/* The default values for the following char* start-up parameters
are determined in innobase_init below: */

char*	innobase_ignored_opt			= NULL;
char*	innobase_data_home_dir			= NULL;
char*	innobase_data_file_path 		= NULL;
#if MYSQL_VERSION_ID < 50600
char*	innobase_log_group_home_dir		= NULL;
#endif
char*	innobase_log_group_home_dir_backup	= NULL;
char*	innobase_log_arch_dir			= NULL;/* unused */
/* The following has a misleading name: starting from 4.0.5, this also
affects Windows: */
char*	innobase_unix_file_flush_method		= NULL;

/* Below we have boolean-valued start-up parameters, and their default
values */

ulong	innobase_fast_shutdown			= 1;
my_bool innobase_log_archive			= FALSE;/* unused */
my_bool innobase_use_doublewrite    = TRUE;
my_bool innobase_use_checksums      = TRUE;
my_bool innobase_use_large_pages    = FALSE;
my_bool	innobase_file_per_table			= FALSE;
my_bool innobase_locks_unsafe_for_binlog        = FALSE;
my_bool innobase_rollback_on_timeout		= FALSE;
my_bool innobase_create_status_file		= FALSE;
my_bool innobase_adaptive_hash_index		= TRUE;

static char *internal_innobase_data_file_path	= NULL;

/* The following counter is used to convey information to InnoDB
about server activity: in selects it is not sensible to call
srv_active_wake_master_thread after each fetch or search, we only do
it every INNOBASE_WAKE_INTERVAL'th step. */

#define INNOBASE_WAKE_INTERVAL	32
ulong	innobase_active_counter	= 0;

static char *xtrabackup_debug_sync = NULL;

/* ======== Datafiles iterator ======== */
typedef struct {
	fil_system_t *system;
	fil_space_t  *space;
	fil_node_t   *node;
	ibool        started;
	os_ib_mutex_t   mutex;
} datafiles_iter_t;

static
datafiles_iter_t *
datafiles_iter_new(fil_system_t *f_system)
{
	datafiles_iter_t *it;

	it = static_cast<datafiles_iter_t *>
		(ut_malloc(sizeof(datafiles_iter_t)));
	it->mutex = OS_MUTEX_CREATE();

	it->system = f_system;
	it->space = NULL;
	it->node = NULL;
	it->started = FALSE;

	return it;
}

static
fil_node_t *
datafiles_iter_next(datafiles_iter_t *it)
{
	fil_node_t *new_node;

	os_mutex_enter(it->mutex);

	if (it->node == NULL) {
		if (it->started)
			goto end;
		it->started = TRUE;
	} else {
		it->node = UT_LIST_GET_NEXT(chain, it->node);
		if (it->node != NULL)
			goto end;
	}

	it->space = (it->space == NULL) ?
		UT_LIST_GET_FIRST(it->system->space_list) :
		UT_LIST_GET_NEXT(space_list, it->space);

	while (it->space != NULL &&
	       (it->space->purpose != FIL_TABLESPACE ||
		UT_LIST_GET_LEN(it->space->chain) == 0))
		it->space = UT_LIST_GET_NEXT(space_list, it->space);
	if (it->space == NULL)
		goto end;

	it->node = UT_LIST_GET_FIRST(it->space->chain);

end:
	new_node = it->node;
	os_mutex_exit(it->mutex);

	return new_node;
}

static
void
datafiles_iter_free(datafiles_iter_t *it)
{
	os_mutex_free(it->mutex);
	ut_free(it);
}

/* ======== Date copying thread context ======== */

typedef struct {
	datafiles_iter_t 	*it;
	uint			num;
	uint			*count;
	os_ib_mutex_t		count_mutex;
	os_thread_id_t		id;
	ds_ctxt_t		*ds_ctxt;
} data_thread_ctxt_t;

/* ======== for option and variables ======== */

enum options_xtrabackup
{
  OPT_XTRA_TARGET_DIR=256,
  OPT_XTRA_BACKUP,
  OPT_XTRA_STATS,
  OPT_XTRA_PREPARE,
  OPT_XTRA_EXPORT,
  OPT_XTRA_APPLY_LOG_ONLY,
  OPT_XTRA_PRINT_PARAM,
  OPT_XTRA_SUSPEND_AT_END,
  OPT_XTRA_USE_MEMORY,
  OPT_XTRA_THROTTLE,
  OPT_XTRA_LOG_ONLY,
  OPT_XTRA_INCREMENTAL,
  OPT_XTRA_INCREMENTAL_BASEDIR,
  OPT_XTRA_EXTRA_LSNDIR,
  OPT_XTRA_INCREMENTAL_DIR,
  OPT_XTRA_TABLES,
  OPT_XTRA_TABLES_FILE,
  OPT_XTRA_CREATE_IB_LOGFILE,
  OPT_XTRA_PARALLEL,
  OPT_XTRA_STREAM,
  OPT_XTRA_COMPRESS,
  OPT_XTRA_COMPRESS_THREADS,
  OPT_INNODB,
  OPT_INNODB_CHECKSUMS,
  OPT_INNODB_DATA_FILE_PATH,
  OPT_INNODB_DATA_HOME_DIR,
  OPT_INNODB_ADAPTIVE_HASH_INDEX,
  OPT_INNODB_DOUBLEWRITE,
  OPT_INNODB_FAST_SHUTDOWN,
  OPT_INNODB_FILE_PER_TABLE,
  OPT_INNODB_FLUSH_LOG_AT_TRX_COMMIT,
  OPT_INNODB_FLUSH_METHOD,
  OPT_INNODB_LOCKS_UNSAFE_FOR_BINLOG,
  OPT_INNODB_LOG_ARCH_DIR,
  OPT_INNODB_LOG_ARCHIVE,
  OPT_INNODB_LOG_GROUP_HOME_DIR,
  OPT_INNODB_MAX_DIRTY_PAGES_PCT,
  OPT_INNODB_MAX_PURGE_LAG,
  OPT_INNODB_ROLLBACK_ON_TIMEOUT,
  OPT_INNODB_STATUS_FILE,
  OPT_INNODB_ADDITIONAL_MEM_POOL_SIZE,
  OPT_INNODB_AUTOEXTEND_INCREMENT,
  OPT_INNODB_BUFFER_POOL_SIZE,
  OPT_INNODB_COMMIT_CONCURRENCY,
  OPT_INNODB_CONCURRENCY_TICKETS,
  OPT_INNODB_FILE_IO_THREADS,
#ifdef INNODB_VERSION_SHORT
  OPT_INNODB_IO_CAPACITY,
  OPT_INNODB_READ_IO_THREADS,
  OPT_INNODB_WRITE_IO_THREADS,
#endif
#if MYSQL_VERSION_ID >= 50500
  OPT_INNODB_USE_NATIVE_AIO,
#endif
#ifdef XTRADB_BASED
  OPT_INNODB_PAGE_SIZE,
  OPT_INNODB_LOG_BLOCK_SIZE,
  OPT_INNODB_FAST_CHECKSUM,
  OPT_INNODB_EXTRA_UNDOSLOTS,
  OPT_INNODB_DOUBLEWRITE_FILE,
#endif
  OPT_INNODB_FORCE_RECOVERY,
  OPT_INNODB_LOCK_WAIT_TIMEOUT,
  OPT_INNODB_LOG_BUFFER_SIZE,
  OPT_INNODB_LOG_FILE_SIZE,
  OPT_INNODB_LOG_FILES_IN_GROUP,
  OPT_INNODB_MIRRORED_LOG_GROUPS,
  OPT_INNODB_OPEN_FILES,
  OPT_INNODB_SYNC_SPIN_LOOPS,
  OPT_INNODB_THREAD_CONCURRENCY,
  OPT_INNODB_THREAD_SLEEP_DELAY,
  OPT_XTRA_DEBUG_SYNC,
#if MYSQL_VERSION_ID >= 50600
  OPT_INNODB_CHECKSUM_ALGORITHM,
  OPT_UNDO_TABLESPACES,
#endif
  OPT_DEFAULTS_GROUP
};

#if MYSQL_VERSION_ID >= 50600
/** Possible values for system variable "innodb_checksum_algorithm". */
static const char* innodb_checksum_algorithm_names[] = {
	"crc32",
	"strict_crc32",
	"innodb",
	"strict_innodb",
	"none",
	"strict_none",
	"facebook",
	NullS
};

/** Used to define an enumerate type of the system variable
    innodb_checksum_algorithm. */
static TYPELIB innodb_checksum_algorithm_typelib = {
	array_elements(innodb_checksum_algorithm_names) - 1,
	"innodb_checksum_algorithm_typelib",
	innodb_checksum_algorithm_names,
	NULL
};
#endif

static struct my_option xb_long_options[] =
{
  {"version", 'v', "print xtrabackup version information",
   (G_PTR *) &xtrabackup_version, (G_PTR *) &xtrabackup_version, 0, GET_BOOL,
   NO_ARG, 0, 0, 0, 0, 0, 0},
  {"target-dir", OPT_XTRA_TARGET_DIR, "destination directory", (G_PTR*) &xtrabackup_target_dir,
   (G_PTR*) &xtrabackup_target_dir, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"backup", OPT_XTRA_BACKUP, "take backup to target-dir",
   (G_PTR*) &xtrabackup_backup, (G_PTR*) &xtrabackup_backup,
   0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"stats", OPT_XTRA_STATS, "calc statistic of datadir (offline mysqld is recommended)",
   (G_PTR*) &xtrabackup_stats, (G_PTR*) &xtrabackup_stats,
   0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"prepare", OPT_XTRA_PREPARE, "prepare a backup for starting mysql server on the backup.",
   (G_PTR*) &xtrabackup_prepare, (G_PTR*) &xtrabackup_prepare,
   0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"export", OPT_XTRA_EXPORT, "create files to import to another database when prepare.",
   (G_PTR*) &xtrabackup_export, (G_PTR*) &xtrabackup_export,
   0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"apply-log-only", OPT_XTRA_APPLY_LOG_ONLY,
   "stop recovery process not to progress LSN after applying log when prepare.",
   (G_PTR*) &xtrabackup_apply_log_only, (G_PTR*) &xtrabackup_apply_log_only,
   0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"print-param", OPT_XTRA_PRINT_PARAM, "print parameter of mysqld needed for copyback.",
   (G_PTR*) &xtrabackup_print_param, (G_PTR*) &xtrabackup_print_param,
   0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"use-memory", OPT_XTRA_USE_MEMORY, "The value is used instead of buffer_pool_size",
   (G_PTR*) &xtrabackup_use_memory, (G_PTR*) &xtrabackup_use_memory,
   0, GET_LL, REQUIRED_ARG, 100*1024*1024L, 1024*1024L, LONGLONG_MAX, 0,
   1024*1024L, 0},
  {"suspend-at-end", OPT_XTRA_SUSPEND_AT_END, "creates a file 'xtrabackup_suspended' and waits until the user deletes that file at the end of '--backup'",
   (G_PTR*) &xtrabackup_suspend_at_end, (G_PTR*) &xtrabackup_suspend_at_end,
   0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"throttle", OPT_XTRA_THROTTLE, "limit count of IO operations (pairs of read&write) per second to IOS values (for '--backup')",
   (G_PTR*) &xtrabackup_throttle, (G_PTR*) &xtrabackup_throttle,
   0, GET_LONG, REQUIRED_ARG, 0, 0, LONG_MAX, 0, 1, 0},
  {"log-stream", OPT_XTRA_LOG_ONLY, "outputs the contents of 'xtrabackup_logfile' to stdout only until the file 'xtrabackup_suspended' deleted (for '--backup').",
   (G_PTR*) &xtrabackup_log_only, (G_PTR*) &xtrabackup_log_only,
   0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"extra-lsndir", OPT_XTRA_EXTRA_LSNDIR, "(for --backup): save an extra copy of the xtrabackup_checkpoints file in this directory.",
   (G_PTR*) &xtrabackup_extra_lsndir, (G_PTR*) &xtrabackup_extra_lsndir,
   0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"incremental-lsn", OPT_XTRA_INCREMENTAL, "(for --backup): copy only .ibd pages newer than specified LSN 'high:low'. ##ATTENTION##: If a wrong LSN value is specified, it is impossible to diagnose this, causing the backup to be unusable. Be careful!",
   (G_PTR*) &xtrabackup_incremental, (G_PTR*) &xtrabackup_incremental,
   0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"incremental-basedir", OPT_XTRA_INCREMENTAL_BASEDIR, "(for --backup): copy only .ibd pages newer than backup at specified directory.",
   (G_PTR*) &xtrabackup_incremental_basedir, (G_PTR*) &xtrabackup_incremental_basedir,
   0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"incremental-dir", OPT_XTRA_INCREMENTAL_DIR, "(for --prepare): apply .delta files and logfile in the specified directory.",
   (G_PTR*) &xtrabackup_incremental_dir, (G_PTR*) &xtrabackup_incremental_dir,
   0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"tables", OPT_XTRA_TABLES, "filtering by regexp for table names.",
   (G_PTR*) &xtrabackup_tables, (G_PTR*) &xtrabackup_tables,
   0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"tables_file", OPT_XTRA_TABLES_FILE, "filtering by list of the exact database.table name in the file.",
   (G_PTR*) &xtrabackup_tables_file, (G_PTR*) &xtrabackup_tables_file,
   0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"create-ib-logfile", OPT_XTRA_CREATE_IB_LOGFILE, "** not work for now** creates ib_logfile* also after '--prepare'. ### If you want create ib_logfile*, only re-execute this command in same options. ###",
   (G_PTR*) &xtrabackup_create_ib_logfile, (G_PTR*) &xtrabackup_create_ib_logfile,
   0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},

  {"datadir", 'h', "Path to the database root.", (G_PTR*) &mysql_data_home,
   (G_PTR*) &mysql_data_home, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"tmpdir", 't',
   "Path for temporary files. Several paths may be specified, separated by a "
#if defined(__WIN__) || defined(OS2) || defined(__NETWARE__)
   "semicolon (;)"
#else
   "colon (:)"
#endif
   ", in this case they are used in a round-robin fashion.",
   (G_PTR*) &opt_mysql_tmpdir,
   (G_PTR*) &opt_mysql_tmpdir, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"parallel", OPT_XTRA_PARALLEL,
   "Number of threads to use for parallel datafiles transfer. Does not have "
   "any effect in the stream mode. The default value is 1.",
   (G_PTR*) &xtrabackup_parallel, (G_PTR*) &xtrabackup_parallel, 0, GET_INT,
   REQUIRED_ARG, 1, 1, INT_MAX, 0, 0, 0},

  {"stream", OPT_XTRA_STREAM, "Stream all backup files to the standard output "
   "in the specified format. Currently the only supported format is 'tar'.",
   (G_PTR*) &xtrabackup_stream_str, (G_PTR*) &xtrabackup_stream_str, 0, GET_STR,
   REQUIRED_ARG, 0, 0, 0, 0, 0, 0},

  {"compress", OPT_XTRA_COMPRESS, "Compress individual backup files using the "
   "specified compression algorithm. Currently the only supported algorithm "
   "is 'quicklz'. It is also the default algorithm, i.e. the one used when "
   "--compress is used without an argument.",
   (G_PTR*) &xtrabackup_compress_alg, (G_PTR*) &xtrabackup_compress_alg, 0,
   GET_STR, OPT_ARG, 0, 0, 0, 0, 0, 0},

  {"compress-threads", OPT_XTRA_COMPRESS_THREADS,
   "Number of threads for parallel data compression. The default value is 1.",
   (G_PTR*) &xtrabackup_compress_threads, (G_PTR*) &xtrabackup_compress_threads,
   0, GET_UINT, REQUIRED_ARG, 1, 1, UINT_MAX, 0, 0, 0},

  {"innodb", OPT_INNODB, "Ignored option for MySQL option compatibility",
   (G_PTR*) &innobase_ignored_opt, (G_PTR*) &innobase_ignored_opt, 0,
   GET_STR, OPT_ARG, 0, 0, 0, 0, 0, 0},

  {"innodb_adaptive_hash_index", OPT_INNODB_ADAPTIVE_HASH_INDEX,
   "Enable InnoDB adaptive hash index (enabled by default).  "
   "Disable with --skip-innodb-adaptive-hash-index.",
   (G_PTR*) &innobase_adaptive_hash_index,
   (G_PTR*) &innobase_adaptive_hash_index,
   0, GET_BOOL, NO_ARG, 1, 0, 0, 0, 0, 0},
  {"innodb_additional_mem_pool_size", OPT_INNODB_ADDITIONAL_MEM_POOL_SIZE,
   "Size of a memory pool InnoDB uses to store data dictionary information and other internal data structures.",
   (G_PTR*) &innobase_additional_mem_pool_size,
   (G_PTR*) &innobase_additional_mem_pool_size, 0, GET_LONG, REQUIRED_ARG,
   1*1024*1024L, 512*1024L, LONG_MAX, 0, 1024, 0},
  {"innodb_autoextend_increment", OPT_INNODB_AUTOEXTEND_INCREMENT,
   "Data file autoextend increment in megabytes",
   (G_PTR*) &srv_auto_extend_increment,
   (G_PTR*) &srv_auto_extend_increment,
   0, GET_ULONG, REQUIRED_ARG, 8L, 1L, 1000L, 0, 1L, 0},
  {"innodb_buffer_pool_size", OPT_INNODB_BUFFER_POOL_SIZE,
   "The size of the memory buffer InnoDB uses to cache data and indexes of its tables.",
   (G_PTR*) &innobase_buffer_pool_size, (G_PTR*) &innobase_buffer_pool_size, 0,
   GET_LL, REQUIRED_ARG, 8*1024*1024L, 1024*1024L, LONGLONG_MAX, 0,
   1024*1024L, 0},
  {"innodb_checksums", OPT_INNODB_CHECKSUMS, "Enable InnoDB checksums validation (enabled by default). \
Disable with --skip-innodb-checksums.", (G_PTR*) &innobase_use_checksums,
   (G_PTR*) &innobase_use_checksums, 0, GET_BOOL, NO_ARG, 1, 0, 0, 0, 0, 0},
/*
  {"innodb_commit_concurrency", OPT_INNODB_COMMIT_CONCURRENCY,
   "Helps in performance tuning in heavily concurrent environments.",
   (G_PTR*) &srv_commit_concurrency, (G_PTR*) &srv_commit_concurrency,
   0, GET_ULONG, REQUIRED_ARG, 0, 0, 1000, 0, 1, 0},
*/
/*
  {"innodb_concurrency_tickets", OPT_INNODB_CONCURRENCY_TICKETS,
   "Number of times a thread is allowed to enter InnoDB within the same \
    SQL query after it has once got the ticket",
   (G_PTR*) &srv_n_free_tickets_to_enter,
   (G_PTR*) &srv_n_free_tickets_to_enter,
   0, GET_ULONG, REQUIRED_ARG, 500L, 1L, ULONG_MAX, 0, 1L, 0},
*/
  {"innodb_data_file_path", OPT_INNODB_DATA_FILE_PATH,
   "Path to individual files and their sizes.", (G_PTR*) &innobase_data_file_path,
   (G_PTR*) &innobase_data_file_path, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"innodb_data_home_dir", OPT_INNODB_DATA_HOME_DIR,
   "The common part for InnoDB table spaces.", (G_PTR*) &innobase_data_home_dir,
   (G_PTR*) &innobase_data_home_dir, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0,
   0},
  {"innodb_doublewrite", OPT_INNODB_DOUBLEWRITE, "Enable InnoDB doublewrite buffer (enabled by default). \
Disable with --skip-innodb-doublewrite.", (G_PTR*) &innobase_use_doublewrite,
   (G_PTR*) &innobase_use_doublewrite, 0, GET_BOOL, NO_ARG, 1, 0, 0, 0, 0, 0},
#ifdef INNODB_VERSION_SHORT
  {"innodb_io_capacity", OPT_INNODB_IO_CAPACITY,
   "Number of IOPs the server can do. Tunes the background IO rate",
   (G_PTR*) &srv_io_capacity, (G_PTR*) &srv_io_capacity,
   0, GET_ULONG, OPT_ARG, 200, 100, ~0UL, 0, 0, 0},
#endif
/*
  {"innodb_fast_shutdown", OPT_INNODB_FAST_SHUTDOWN,
   "Speeds up the shutdown process of the InnoDB storage engine. Possible "
   "values are 0, 1 (faster)"
   " or 2 (fastest - crash-like)"
   ".",
   (G_PTR*) &innobase_fast_shutdown,
   (G_PTR*) &innobase_fast_shutdown, 0, GET_ULONG, OPT_ARG, 1, 0,
   2, 0, 0, 0},
*/
  {"innodb_file_io_threads", OPT_INNODB_FILE_IO_THREADS,
   "Number of file I/O threads in InnoDB.", (G_PTR*) &innobase_file_io_threads,
   (G_PTR*) &innobase_file_io_threads, 0, GET_LONG, REQUIRED_ARG, 4, 4, 64, 0,
   1, 0},
#ifdef INNODB_VERSION_SHORT
  {"innodb_read_io_threads", OPT_INNODB_READ_IO_THREADS,
   "Number of background read I/O threads in InnoDB.", (G_PTR*) &innobase_read_io_threads,
   (G_PTR*) &innobase_read_io_threads, 0, GET_LONG, REQUIRED_ARG, 4, 1, 64, 0,
   1, 0},
  {"innodb_write_io_threads", OPT_INNODB_WRITE_IO_THREADS,
   "Number of background write I/O threads in InnoDB.", (G_PTR*) &innobase_write_io_threads,
   (G_PTR*) &innobase_write_io_threads, 0, GET_LONG, REQUIRED_ARG, 4, 1, 64, 0,
   1, 0},
#endif
  {"innodb_file_per_table", OPT_INNODB_FILE_PER_TABLE,
   "Stores each InnoDB table to an .ibd file in the database dir.",
   (G_PTR*) &innobase_file_per_table,
   (G_PTR*) &innobase_file_per_table, 0, GET_BOOL, NO_ARG,
   FALSE, 0, 0, 0, 0, 0},
  {"innodb_flush_log_at_trx_commit", OPT_INNODB_FLUSH_LOG_AT_TRX_COMMIT,
   "Set to 0 (write and flush once per second), 1 (write and flush at each commit) or 2 (write at commit, flush once per second).",
   (G_PTR*) &srv_flush_log_at_trx_commit,
   (G_PTR*) &srv_flush_log_at_trx_commit,
   0, GET_ULONG, OPT_ARG,  1, 0, 2, 0, 0, 0},
  {"innodb_flush_method", OPT_INNODB_FLUSH_METHOD,
   "With which method to flush data.", (G_PTR*) &innobase_unix_file_flush_method,
   (G_PTR*) &innobase_unix_file_flush_method, 0, GET_STR, REQUIRED_ARG, 0, 0, 0,
   0, 0, 0},

/* ####### Should we use this option? ####### */
  {"innodb_force_recovery", OPT_INNODB_FORCE_RECOVERY,
   "Helps to save your data in case the disk image of the database becomes corrupt.",
   (G_PTR*) &innobase_force_recovery, (G_PTR*) &innobase_force_recovery, 0,
   GET_LONG, REQUIRED_ARG, 0, 0, 6, 0, 1, 0},

  {"innodb_lock_wait_timeout", OPT_INNODB_LOCK_WAIT_TIMEOUT,
   "Timeout in seconds an InnoDB transaction may wait for a lock before being rolled back.",
   (G_PTR*) &innobase_lock_wait_timeout, (G_PTR*) &innobase_lock_wait_timeout,
   0, GET_LONG, REQUIRED_ARG, 50, 1, 1024 * 1024 * 1024, 0, 1, 0},
/*
  {"innodb_locks_unsafe_for_binlog", OPT_INNODB_LOCKS_UNSAFE_FOR_BINLOG,
   "Force InnoDB not to use next-key locking. Instead use only row-level locking",
   (G_PTR*) &innobase_locks_unsafe_for_binlog,
   (G_PTR*) &innobase_locks_unsafe_for_binlog, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
*/
/*
  {"innodb_log_arch_dir", OPT_INNODB_LOG_ARCH_DIR,
   "Where full logs should be archived.", (G_PTR*) &innobase_log_arch_dir,
   (G_PTR*) &innobase_log_arch_dir, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
*/
  {"innodb_log_buffer_size", OPT_INNODB_LOG_BUFFER_SIZE,
   "The size of the buffer which InnoDB uses to write log to the log files on disk.",
   (G_PTR*) &innobase_log_buffer_size, (G_PTR*) &innobase_log_buffer_size, 0,
   GET_LONG, REQUIRED_ARG, 1024*1024L, 256*1024L, LONG_MAX, 0, 1024, 0},
  {"innodb_log_file_size", OPT_INNODB_LOG_FILE_SIZE,
   "Size of each log file in a log group.",
   (G_PTR*) &innobase_log_file_size, (G_PTR*) &innobase_log_file_size, 0,
   GET_LL, REQUIRED_ARG, DEFAULT_LOG_FILE_SIZE, 1*1024*1024L, LONGLONG_MAX, 0,
   1024*1024L, 0},
  {"innodb_log_files_in_group", OPT_INNODB_LOG_FILES_IN_GROUP,
   "Number of log files in the log group. InnoDB writes to the files in a circular fashion. Value 3 is recommended here.",
   (G_PTR*) &innobase_log_files_in_group, (G_PTR*) &innobase_log_files_in_group,
   0, GET_LONG, REQUIRED_ARG, 2, 2, 100, 0, 1, 0},
  {"innodb_log_group_home_dir", OPT_INNODB_LOG_GROUP_HOME_DIR,
   "Path to InnoDB log files.", (G_PTR*) &INNODB_LOG_DIR,
   (G_PTR*) &INNODB_LOG_DIR, 0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0,
   0, 0},
  {"innodb_max_dirty_pages_pct", OPT_INNODB_MAX_DIRTY_PAGES_PCT,
   "Percentage of dirty pages allowed in bufferpool.", (G_PTR*) &srv_max_buf_pool_modified_pct,
   (G_PTR*) &srv_max_buf_pool_modified_pct, 0, GET_ULONG, REQUIRED_ARG, 90, 0, 100, 0, 0, 0},
/*
  {"innodb_max_purge_lag", OPT_INNODB_MAX_PURGE_LAG,
   "Desired maximum length of the purge queue (0 = no limit)",
   (G_PTR*) &srv_max_purge_lag,
   (G_PTR*) &srv_max_purge_lag, 0, GET_ULONG, REQUIRED_ARG, 0, 0, ULONG_MAX,
   0, 1L, 0},
*/
/*
  {"innodb_mirrored_log_groups", OPT_INNODB_MIRRORED_LOG_GROUPS,
   "Number of identical copies of log groups we keep for the database. Currently this should be set to 1.",
   (G_PTR*) &innobase_mirrored_log_groups,
   (G_PTR*) &innobase_mirrored_log_groups, 0, GET_LONG, REQUIRED_ARG, 1, 1, 10,
   0, 1, 0},
*/
  {"innodb_open_files", OPT_INNODB_OPEN_FILES,
   "How many files at the maximum InnoDB keeps open at the same time.",
   (G_PTR*) &innobase_open_files, (G_PTR*) &innobase_open_files, 0,
   GET_LONG, REQUIRED_ARG, 300L, 10L, LONG_MAX, 0, 1L, 0},
#if MYSQL_VERSION_ID >= 50500
  {"innodb_use_native_aio", OPT_INNODB_USE_NATIVE_AIO,
   "Use native AIO if supported on this platform.",
   (G_PTR*) &srv_use_native_aio,
   (G_PTR*) &srv_use_native_aio, 0, GET_BOOL, NO_ARG,
   FALSE, 0, 0, 0, 0, 0},
#endif
#ifdef XTRADB_BASED
  {"innodb_page_size", OPT_INNODB_PAGE_SIZE,
   "The universal page size of the database.",
   (G_PTR*) &innobase_page_size, (G_PTR*) &innobase_page_size, 0,
   GET_LONG, REQUIRED_ARG, (1 << 14), (1 << 12), (1 << UNIV_PAGE_SIZE_SHIFT_MAX), 0, 1L, 0},
  {"innodb_log_block_size", OPT_INNODB_LOG_BLOCK_SIZE,
  "The log block size of the transaction log file. "
   "Changing for created log file is not supported. Use on your own risk!",
   (G_PTR*) &innobase_log_block_size, (G_PTR*) &innobase_log_block_size, 0,
   GET_ULONG, REQUIRED_ARG, 512, 512, 1 << UNIV_PAGE_SIZE_SHIFT_MAX, 0, 1L, 0},
  {"innodb_fast_checksum", OPT_INNODB_FAST_CHECKSUM,
   "Change the algorithm of checksum for the whole of datapage to 4-bytes word based.",
   (G_PTR*) &innobase_fast_checksum,
   (G_PTR*) &innobase_fast_checksum, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"innodb_extra_undoslots", OPT_INNODB_EXTRA_UNDOSLOTS,
   "Enable to use about 4000 undo slots instead of default 1024. Not recommended to use, "
   "Because it is not change back to disable, once it is used.",
   (G_PTR*) &innobase_extra_undoslots, (G_PTR*) &innobase_extra_undoslots,
   0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"innodb_doublewrite_file", OPT_INNODB_DOUBLEWRITE_FILE,
   "Path to special datafile for doublewrite buffer. (default is "": not used)",
   (G_PTR*) &innobase_doublewrite_file, (G_PTR*) &innobase_doublewrite_file,
   0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
#endif

#ifndef __WIN__
  {"debug-sync", OPT_XTRA_DEBUG_SYNC,
   "Debug sync point. This is only used by the xtrabackup test suite",
   (G_PTR*) &xtrabackup_debug_sync,
   (G_PTR*) &xtrabackup_debug_sync,
   0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
#endif
#if MYSQL_VERSION_ID >= 50600
  {"checksum-algorithm", OPT_INNODB_CHECKSUM_ALGORITHM,
  "The algorithm InnoDB uses for page checksumming. [CRC32, STRICT_CRC32, "
   "INNODB, STRICT_INNODB, NONE, STRICT_NONE, FACEBOOK]",
   &srv_checksum_algorithm, &srv_checksum_algorithm,
   &innodb_checksum_algorithm_typelib, GET_ENUM,
   REQUIRED_ARG, 6, 0, 6, 0, 1, 0},
  {"undo-tablespaces", OPT_UNDO_TABLESPACES,
   "Number of undo tablespaces to use. NON-ZERO VALUES ARE NOT "
   "CURRENTLY SUPPORTED",
   (G_PTR*)&srv_undo_tablespaces, (G_PTR*)&srv_undo_tablespaces,
   0, GET_ULONG, REQUIRED_ARG, 0, 0, 126, 0, 1, 0},
#endif
  {"defaults_group", OPT_DEFAULTS_GROUP, "defaults group in config file (default \"mysqld\").",
   (G_PTR*) &defaults_group, (G_PTR*) &defaults_group,
   0, GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  { 0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};

#ifndef __WIN__
static int debug_sync_resumed;

static void sigcont_handler(int sig);

static void sigcont_handler(int sig __attribute__((unused)))
{
	debug_sync_resumed= 1;
}
#endif

static
void
debug_sync_point(const char *name)
{
#ifndef __WIN__
	FILE	*fp;
	pid_t	pid;
	char	pid_path[FN_REFLEN];

	if (xtrabackup_debug_sync == NULL) {
		return;
	}

	if (strcmp(xtrabackup_debug_sync, name)) {
		return;
	}

	pid = getpid();

	snprintf(pid_path, sizeof(pid_path), "%s/xtrabackup_debug_sync",
		 xtrabackup_target_dir);
	fp = fopen(pid_path, "w");
	if (fp == NULL) {
		msg("xtrabackup: Error: cannot open %s\n", pid_path);
		exit(EXIT_FAILURE);
	}
	fprintf(fp, "%u\n", (uint) pid);
	fclose(fp);

	msg("xtrabackup: DEBUG: Suspending at debug sync point '%s'. "
	    "Resume with 'kill -SIGCONT %u'.\n", name, (uint) pid);

	debug_sync_resumed= 0;
	kill(pid, SIGSTOP);
	while (!debug_sync_resumed) {
		sleep(1);
	}

	/* On resume */
	msg("xtrabackup: DEBUG: removing the pid file.\n");
	my_delete(pid_path, MYF(MY_WME));
#endif
}

static const char *xb_load_default_groups[]= { "mysqld", "xtrabackup", 0, 0 };

static void print_version(void)
{
#ifdef XTRADB_BASED
  msg("%s version %s for Percona Server %s %s (%s) (revision id: %s)\n",
      my_progname, XTRABACKUP_VERSION, MYSQL_SERVER_VERSION, SYSTEM_TYPE,
      MACHINE_TYPE, XTRABACKUP_REVISION);
#else
  msg("%s version %s for MySQL server %s %s (%s) (revision id: %s)\n",
      my_progname, XTRABACKUP_VERSION, MYSQL_SERVER_VERSION, SYSTEM_TYPE,
      MACHINE_TYPE, XTRABACKUP_REVISION);
#endif
}

static void usage(void)
{
  puts("Open source backup tool for InnoDB and XtraDB\n\
\n\
Copyright (C) 2009-2012 Percona Ireland Ltd.\n\
Portions Copyright (C) 2000, 2011, MySQL AB & Innobase Oy. All Rights Reserved.\n\
\n\
This program is free software; you can redistribute it and/or\n\
modify it under the terms of the GNU General Public License\n\
as published by the Free Software Foundation version 2\n\
of the License.\n\
\n\
This program is distributed in the hope that it will be useful,\n\
but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n\
GNU General Public License for more details.\n\
\n\
You can download full text of the license on http://www.gnu.org/licenses/gpl-2.0.txt\n");

  printf("Usage: [%s [--defaults-file=#] --backup | %s [--defaults-file=#] --prepare] [OPTIONS]\n",my_progname,my_progname);
  print_defaults("my", xb_load_default_groups);
  my_print_help(xb_long_options);
  my_print_variables(xb_long_options);
}

static my_bool
get_one_option(int optid, const struct my_option *opt __attribute__((unused)),
	       char *argument)
{
  switch(optid) {
  case 'h':
    strmake(mysql_real_data_home,argument, FN_REFLEN - 1);
    mysql_data_home= mysql_real_data_home;
    break;
  case OPT_XTRA_TARGET_DIR:
    strmake(xtrabackup_real_target_dir,argument, sizeof(xtrabackup_real_target_dir)-1);
    xtrabackup_target_dir= xtrabackup_real_target_dir;
    break;
  case OPT_XTRA_STREAM:
    if (!strcasecmp(argument, "tar"))
      xtrabackup_stream_fmt = XB_STREAM_FMT_TAR;
    else if (!strcasecmp(argument, "xbstream"))
      xtrabackup_stream_fmt = XB_STREAM_FMT_XBSTREAM;
    else
    {
      msg("Invalid --stream argument: %s\n", argument);
      return 1;
    }
    xtrabackup_stream = TRUE;
    break;
  case OPT_XTRA_COMPRESS:
    if (argument == NULL)
      xtrabackup_compress_alg = "quicklz";
    else if (strcasecmp(argument, "quicklz"))
    {
      msg("Invalid --compress argument: %s\n", argument);
      return 1;
    }
    xtrabackup_compress = TRUE;
    break;
  case '?':
    usage();
    exit(EXIT_SUCCESS);
    break;
  case 'v':
    print_version();
    exit(EXIT_SUCCESS);
    break;
  default:
    break;
  }
  return 0;
}

/* ================ Dummys =================== */

#if MYSQL_VERSION_ID < 50600

extern "C" {

ibool
thd_is_replication_slave_thread(
	void*	thd)
{
	(void)thd;
	msg("xtrabackup: thd_is_replication_slave_thread() is called\n");
	return(FALSE);
}

ibool
thd_has_edited_nontrans_tables(
	void*	thd)
{
	(void)thd;
	msg("xtrabackup: thd_has_edited_nontrans_tables() is called\n");
	return(FALSE);
}

ibool
thd_is_select(
	const void*	thd)
{
	(void)thd;
	msg("xtrabackup: thd_is_select() is called\n");
	return(FALSE);
}

void
innobase_mysql_print_thd(
	FILE*   f,
	void*   input_thd,
	uint	max_query_len)
{
	(void)f;
	(void)input_thd;
	(void)max_query_len;
	msg("xtrabackup: innobase_mysql_print_thd() is called\n");
}

void
innobase_get_cset_width(
	ulint	cset,
	ulint*	mbminlen,
	ulint*	mbmaxlen)
{
	CHARSET_INFO*	cs;
	ut_ad(cset < 256);
	ut_ad(mbminlen);
	ut_ad(mbmaxlen);

	cs = all_charsets[cset];
	if (cs) {
		*mbminlen = cs->mbminlen;
		*mbmaxlen = cs->mbmaxlen;
	} else {
		ut_a(cset == 0);
		*mbminlen = *mbmaxlen = 0;
	}
}

void
innobase_convert_from_table_id(
#ifdef INNODB_VERSION_SHORT
	struct charset_info_st*	cs,
#endif
	char*	to,
	const char*	from,
	ulint	len)
{
#ifdef INNODB_VERSION_SHORT
	(void)cs;
#endif
	(void)to;
	(void)from;
	(void)len;

	msg("xtrabackup: innobase_convert_from_table_id() is called\n");
}

void
innobase_convert_from_id(
#ifdef INNODB_VERSION_SHORT
	struct charset_info_st*	cs,
#endif
	char*	to,
	const char*	from,
	ulint	len)
{
#ifdef INNODB_VERSION_SHORT
	(void)cs;
#endif
	(void)to;
	(void)from;
	(void)len;
	msg("xtrabackup: innobase_convert_from_id() is called\n");
}

int
innobase_strcasecmp(
	const char*	a,
	const char*	b)
{
	return(my_strcasecmp(&my_charset_utf8_general_ci, a, b));
}

void
innobase_casedn_str(
	char*	a)
{
	my_casedn_str(&my_charset_utf8_general_ci, a);
}

struct charset_info_st*
innobase_get_charset(
	void*   mysql_thd)
{
	(void)mysql_thd;
	msg("xtrabackup: innobase_get_charset() is called\n");
	return(NULL);
}

#ifdef INNODB_VERSION_SHORT

const char*
innobase_get_stmt(
	void*	mysql_thd,
	size_t*	length)
{
	(void)mysql_thd;
	(void)length;
	msg("xtrabackup: innobase_get_stmt() is called\n");
	return("nothing");
}

#endif /* INNODB_VERSION_SHORT */

int
innobase_mysql_tmpfile(void)
{
	char	filename[FN_REFLEN];
	int	fd2 = -1;
	File	fd = create_temp_file(filename, my_tmpdir(&mysql_tmpdir_list), "ib",
#ifdef __WIN__
				O_BINARY | O_TRUNC | O_SEQUENTIAL |
				O_TEMPORARY | O_SHORT_LIVED |
#endif /* __WIN__ */
				O_CREAT | O_EXCL | O_RDWR,
				MYF(MY_WME));
	if (fd >= 0) {
#ifndef __WIN__
		/* On Windows, open files cannot be removed, but files can be
		created with the O_TEMPORARY flag to the same effect
		("delete on close"). */
		unlink(filename);
#endif /* !__WIN__ */
		/* Copy the file descriptor, so that the additional resources
		allocated by create_temp_file() can be freed by invoking
		my_close().

		Because the file descriptor returned by this function
		will be passed to fdopen(), it will be closed by invoking
		fclose(), which in turn will invoke close() instead of
		my_close(). */
#ifdef _WIN32
		/* Note that on Windows, the integer returned by mysql_tmpfile
		has no relation to C runtime file descriptor. Here, we need
		to call my_get_osfhandle to get the HANDLE and then convert it
		to C runtime filedescriptor. */
		{
			HANDLE hFile = my_get_osfhandle(fd);
			HANDLE hDup;
			BOOL bOK =
				DuplicateHandle(GetCurrentProcess(), hFile,
						GetCurrentProcess(), &hDup, 0,
						FALSE, DUPLICATE_SAME_ACCESS);
			if(bOK) {
				fd2 = _open_osfhandle((intptr_t)hDup,0);
			}
			else {
				my_osmaperr(GetLastError());
				fd2 = -1;
			}
		}
#else
		fd2 = dup(fd);
#endif
		if (fd2 < 0) {
			msg("xtrabackup: Got error %d on dup\n",fd2);
                }
		my_close(fd, MYF(MY_WME));
	}
	return(fd2);
}

} /* extern "C" */

#endif /* MYSQL_VERSION_ID < 50600 */

#if MYSQL_VERSION_ID >= 50600

extern os_file_t	files[1000];

/*********************************************************************//**
Creates or opens the log files and closes them.
@return	DB_SUCCESS or error code */
static
ulint
open_or_create_log_file(
/*====================*/
	ibool	create_new_db,		/*!< in: TRUE if we should create a
					  new database */
	ibool*	log_file_created,	/*!< out: TRUE if new log file
					  created */
	ibool	log_file_has_been_opened,/*!< in: TRUE if a log file has been
					   opened before: then it is an error
					   to try to create another log file */
	ulint	k,			/*!< in: log group number */
	ulint	i)			/*!< in: log file number in group */
{
	ibool	ret;
	os_offset_t	size;
	char	name[10000];
	ulint	dirnamelen;

	UT_NOT_USED(create_new_db);
	UT_NOT_USED(log_file_has_been_opened);
	UT_NOT_USED(k);
	ut_ad(k == 0);

	*log_file_created = FALSE;

	srv_normalize_path_for_win(srv_log_group_home_dir);

	dirnamelen = strlen(srv_log_group_home_dir);
	ut_a(dirnamelen < (sizeof name) - 10 - sizeof "ib_logfile");
	memcpy(name, srv_log_group_home_dir, dirnamelen);

	/* Add a path separator if needed. */
	if (dirnamelen && name[dirnamelen - 1] != SRV_PATH_SEPARATOR) {
		name[dirnamelen++] = SRV_PATH_SEPARATOR;
	}

	sprintf(name + dirnamelen, "%s%lu", "ib_logfile", (ulong) i);

	files[i] = os_file_create(innodb_file_log_key, name,
				  OS_FILE_OPEN, OS_FILE_NORMAL,
				  OS_LOG_FILE, &ret);
	if (ret == FALSE) {
		fprintf(stderr, "InnoDB: Error in opening %s\n", name);

		return(DB_ERROR);
	}

	size = os_file_get_size(files[i]);

	if (size != srv_log_file_size * UNIV_PAGE_SIZE) {

		fprintf(stderr,
			"InnoDB: Error: log file %s is"
			" of different size " UINT64PF " bytes\n"
			"InnoDB: than specified in the .cnf"
			" file " UINT64PF " bytes!\n",
			name, size, srv_log_file_size * UNIV_PAGE_SIZE);

		return(DB_ERROR);
	}

	ret = os_file_close(files[i]);
	ut_a(ret);

	if (i == 0) {
		/* Create in memory the file space object
		   which is for this log group */

		fil_space_create(name,
				 2 * k + SRV_LOG_SPACE_FIRST_ID, 0, FIL_LOG);
	}

	ut_a(fil_validate());

	ut_a(fil_node_create(name, srv_log_file_size,
			     2 * k + SRV_LOG_SPACE_FIRST_ID, FALSE));
	if (i == 0) {
		log_group_init(k, srv_n_log_files,
			       srv_log_file_size * UNIV_PAGE_SIZE,
			       2 * k + SRV_LOG_SPACE_FIRST_ID,
			       SRV_LOG_SPACE_FIRST_ID + 1); /* dummy arch
							       space id */
	}

	return(DB_SUCCESS);
}

#endif

/***********************************************************************
Creates a temporary file in tmpdir with a specified prefix in the file
name. The file will be automatically removed on close.
Unlike innobase_mysql_tmpfile(), dup() is not used, so the returned
file must be closed with my_close().
@return file descriptor or a negative number in case of error.*/
static
File
xtrabackup_create_tmpfile(char *path, const char *prefix)
{
	File	fd = create_temp_file(path, xtrabackup_target_dir,
				      prefix,
#ifdef __WIN__
				O_BINARY | O_TRUNC | O_SEQUENTIAL |
				O_TEMPORARY | O_SHORT_LIVED |
#endif /* __WIN__ */
				O_CREAT | O_EXCL | O_RDWR,
				MYF(MY_WME));
#ifndef __WIN__
	if (fd >= 0) {
		/* On Windows, open files cannot be removed, but files can be
		created with the O_TEMPORARY flag to the same effect
		("delete on close"). */
		unlink(path);
	}
#endif /* !__WIN__ */

	return(fd);
}

#if MYSQL_VERSION_ID < 50600

extern "C" {

void
innobase_invalidate_query_cache(
	trx_t*	trx,
#ifndef INNODB_VERSION_SHORT
	char*	full_name,
#else
	const char*	full_name,
#endif
	ulint	full_name_len)
{
	(void)trx;
	(void)full_name;
	(void)full_name_len;
	/* do nothing */
}

#if MYSQL_VERSION_ID >= 50500
/**********************************************************************//**
It should be safe to use lower_case_table_names=0 for xtrabackup. If it causes
any problems, we can add the lower_case_table_names option to xtrabackup
later.
@return	0 */
ulint
innobase_get_lower_case_table_names(void)
/*=====================================*/
{
	return(0);
}

/******************************************************************//**
Strip dir name from a full path name and return only the file name
@return file name or "null" if no file name */
const char*
innobase_basename(
/*==============*/
	const char*	path_name)	/*!< in: full path name */
{
	const char*	name = base_name(path_name);

	return((name) ? name : "null");
}
#endif

} /* extern "C" */

/*****************************************************************//**
Convert an SQL identifier to the MySQL system_charset_info (UTF-8)
and quote it if needed.
@return	pointer to the end of buf */
static
char*
innobase_convert_identifier(
/*========================*/
	char*		buf,	/*!< out: buffer for converted identifier */
	ulint		buflen,	/*!< in: length of buf, in bytes */
	const char*	id,	/*!< in: identifier to convert */
	ulint		idlen,	/*!< in: length of id, in bytes */
	void*		thd __attribute__((unused)), 
						/*!< in: MySQL connection thread, or NULL */
	ibool		file_id __attribute__((unused)))
						/*!< in: TRUE=id is a table or database name;
						FALSE=id is an UTF-8 string */
{
	const char*	s	= id;
	int		q;

	/* See if the identifier needs to be quoted. */
	q = '"';

	if (q == EOF) {
		if (UNIV_UNLIKELY(idlen > buflen)) {
			idlen = buflen;
		}
		memcpy(buf, s, idlen);
		return(buf + idlen);
	}

	/* Quote the identifier. */
	if (buflen < 2) {
		return(buf);
	}

	*buf++ = q;
	buflen--;

	for (; idlen; idlen--) {
		int	c = *s++;
		if (UNIV_UNLIKELY(c == q)) {
			if (UNIV_UNLIKELY(buflen < 3)) {
				break;
			}

			*buf++ = c;
			*buf++ = c;
			buflen -= 2;
		} else {
			if (UNIV_UNLIKELY(buflen < 2)) {
				break;
			}

			*buf++ = c;
			buflen--;
		}
	}

	*buf++ = q;
	return(buf);
}

extern "C" {

/*****************************************************************//**
Convert a table or index name to the MySQL system_charset_info (UTF-8)
and quote it if needed.
@return	pointer to the end of buf */
char*
innobase_convert_name(
/*==================*/
	char*		buf,	/*!< out: buffer for converted identifier */
	ulint		buflen,	/*!< in: length of buf, in bytes */
	const char*	id,	/*!< in: identifier to convert */
	ulint		idlen,	/*!< in: length of id, in bytes */
	void*		thd,	/*!< in: MySQL connection thread, or NULL */
	ibool		table_id)/*!< in: TRUE=id is a table or database name;
				FALSE=id is an index name */
{
	char*		s	= buf;
	const char*	bufend	= buf + buflen;

	if (table_id) {
		const char*	slash = (const char*) memchr(id, '/', idlen);
		if (!slash) {

			goto no_db_name;
		}

		/* Print the database name and table name separately. */
		s = innobase_convert_identifier(s, bufend - s, id, slash - id,
						thd, TRUE);
		if (UNIV_LIKELY(s < bufend)) {
			*s++ = '.';
			s = innobase_convert_identifier(s, bufend - s,
							slash + 1, idlen
							- (slash - id) - 1,
							thd, TRUE);
		}
#ifdef INNODB_VERSION_SHORT
	} else if (UNIV_UNLIKELY(*id == TEMP_INDEX_PREFIX)) {
		/* Temporary index name (smart ALTER TABLE) */
		const char temp_index_suffix[]= "--temporary--";

		s = innobase_convert_identifier(buf, buflen, id + 1, idlen - 1,
						thd, FALSE);
		if (s - buf + (sizeof temp_index_suffix - 1) < buflen) {
			memcpy(s, temp_index_suffix,
			       sizeof temp_index_suffix - 1);
			s += sizeof temp_index_suffix - 1;
		}
#endif
	} else {
no_db_name:
		s = innobase_convert_identifier(buf, buflen, id, idlen,
						thd, table_id);
	}

	return(s);

}

ibool
trx_is_interrupted(
	trx_t*	trx)
{
	(void)trx;
	/* There are no mysql_thd */
	return(FALSE);
}

int
innobase_mysql_cmp(
	int		mysql_type,
	uint		charset_number,
	unsigned char*	a,
	unsigned int	a_length,
	unsigned char*	b,
	unsigned int	b_length)
{
	CHARSET_INFO*		charset;
	enum enum_field_types	mysql_tp;
	int                     ret;

	DBUG_ASSERT(a_length != UNIV_SQL_NULL);
	DBUG_ASSERT(b_length != UNIV_SQL_NULL);

	mysql_tp = (enum enum_field_types) mysql_type;

	switch (mysql_tp) {

        case MYSQL_TYPE_BIT:
	case MYSQL_TYPE_STRING:
	case MYSQL_TYPE_VAR_STRING:
	case FIELD_TYPE_TINY_BLOB:
	case FIELD_TYPE_MEDIUM_BLOB:
	case FIELD_TYPE_BLOB:
	case FIELD_TYPE_LONG_BLOB:
        case MYSQL_TYPE_VARCHAR:
		/* Use the charset number to pick the right charset struct for
		the comparison. Since the MySQL function get_charset may be
		slow before Bar removes the mutex operation there, we first
		look at 2 common charsets directly. */

		if (charset_number == default_charset_info->number) {
			charset = default_charset_info;
		} else if (charset_number == my_charset_latin1.number) {
			charset = &my_charset_latin1;
		} else {
			charset = get_charset(charset_number, MYF(MY_WME));

			if (charset == NULL) {
				msg("xtrabackup: InnoDB needs charset %lu for "
				    "doing a comparison, but MySQL cannot "
				    "find that charset.\n",
				    (ulong) charset_number);
				ut_a(0);
			}
		}

                /* Starting from 4.1.3, we use strnncollsp() in comparisons of
                non-latin1_swedish_ci strings. NOTE that the collation order
                changes then: 'b\0\0...' is ordered BEFORE 'b  ...'. Users
                having indexes on such data need to rebuild their tables! */

                ret = charset->coll->strnncollsp(charset,
                                  a, a_length,
                                                 b, b_length, 0);
		if (ret < 0) {
		        return(-1);
		} else if (ret > 0) {
		        return(1);
		} else {
		        return(0);
	        }
	default:
		assert(0);
	}

	return(0);
}

ulint
innobase_get_at_most_n_mbchars(
	ulint charset_id,
	ulint prefix_len,
	ulint data_len,
	const char* str)
{
	ulint char_length;	/* character length in bytes */
	ulint n_chars;		/* number of characters in prefix */
	CHARSET_INFO* charset;	/* charset used in the field */

	charset = get_charset((uint) charset_id, MYF(MY_WME));

	ut_ad(charset);
	ut_ad(charset->mbmaxlen);

	/* Calculate how many characters at most the prefix index contains */

	n_chars = prefix_len / charset->mbmaxlen;

	/* If the charset is multi-byte, then we must find the length of the
	first at most n chars in the string. If the string contains less
	characters than n, then we return the length to the end of the last
	character. */

	if (charset->mbmaxlen > 1) {
		/* my_charpos() returns the byte length of the first n_chars
		characters, or a value bigger than the length of str, if
		there were not enough full characters in str.

		Why does the code below work:
		Suppose that we are looking for n UTF-8 characters.

		1) If the string is long enough, then the prefix contains at
		least n complete UTF-8 characters + maybe some extra
		characters + an incomplete UTF-8 character. No problem in
		this case. The function returns the pointer to the
		end of the nth character.

		2) If the string is not long enough, then the string contains
		the complete value of a column, that is, only complete UTF-8
		characters, and we can store in the column prefix index the
		whole string. */

		char_length = my_charpos(charset, str,
						str + data_len, (int) n_chars);
		if (char_length > data_len) {
			char_length = data_len;
		}
	} else {
		if (data_len < prefix_len) {
			char_length = data_len;
		} else {
			char_length = prefix_len;
		}
	}

	return(char_length);
}

}

#endif /* MYSQL_VERSION_ID < 50600 */

#ifdef INNODB_VERSION_SHORT
#if MYSQL_VERSION_ID < 50600

extern "C" {

ulint
innobase_raw_format(
/*================*/
	const char*	data,		/*!< in: raw data */
	ulint		data_len,	/*!< in: raw data length
					in bytes */
	ulint		charset_coll,	/*!< in: charset collation */
	char*		buf,		/*!< out: output buffer */
	ulint		buf_size)	/*!< in: output buffer size
					in bytes */
{
	(void)data;
	(void)data_len;
	(void)charset_coll;
	(void)buf;
	(void)buf_size;

	msg("xtrabackup: innobase_raw_format() is called\n");
	return(0);
}

ulong
thd_lock_wait_timeout(
/*==================*/
	void*	thd)	/*!< in: thread handle (THD*), or NULL to query
			the global innodb_lock_wait_timeout */
{
	(void)thd;
	return(innobase_lock_wait_timeout);
}

ibool
thd_supports_xa(
/*============*/
	void*	thd)	/*!< in: thread handle (THD*), or NULL to query
			the global innodb_supports_xa */
{
	(void)thd;
	return(FALSE);
}

ibool
trx_is_strict(
/*==========*/
	trx_t*	trx)	/*!< in: transaction */
{
	(void)trx;
	return(FALSE);
}

#endif /* MYSQL_VERSION_ID < 50600 */

#ifdef XTRADB_BASED
trx_t*
innobase_get_trx()
{
	return(NULL);
}

ibool
innobase_get_slow_log()
{
	return(FALSE);
}
#endif /* XTRADB_BASED */

#if MYSQL_VERSION_ID < 50600
} /* extern "C" */
#endif

#endif /* INNODB_VERSION_SHORT */


/***********************************************************************//**
Compatibility wrapper around os_file_flush().
@return	TRUE if success */
static
ibool
xb_file_flush(
/*==========*/
	os_file_t	file)	/*!< in, own: handle to a file */
{
#ifdef XTRADB_BASED
	return os_file_flush(file, TRUE);
#else
	return os_file_flush(file);
#endif
}

/***********************************************************************
Computes bit shift for a given value. If the argument is not a power
of 2, returns 0.*/
static inline
ulint
get_bit_shift(ulint value)
{
	ulint shift;

	if (value == 0)
		return 0;

	for (shift = 0; !(value & 1UL); shift++) {
		value >>= 1;
	}
	return (value >> 1) ? 0 : shift;
}

/***********************************************************************
Initializes log_block_size*/
static
ibool
xb_init_log_block_size(void)
{
#ifdef XTRADB_BASED
	srv_log_block_size = 0;
	if (innobase_log_block_size != 512) {
		uint	n_shift = get_bit_shift(innobase_log_block_size);;

		if (n_shift > 0) {
			srv_log_block_size = (1 << n_shift);
			msg("InnoDB: The log block size is set to %lu.\n",
			    srv_log_block_size);
		}
	} else {
		srv_log_block_size = 512;
	}
	if (!srv_log_block_size) {
		msg("InnoDB: Error: %lu is not valid value for "
		    "innodb_log_block_size.\n", innobase_log_block_size);
		return FALSE;
	}
#endif
	return TRUE;
}

static my_bool
innodb_init_param(void)
{
	/* innobase_init */
	static char	current_dir[3];		/* Set if using current lib */
	my_bool		ret;
	char		*default_path;

	/* === some variables from mysqld === */
	memset((G_PTR) &mysql_tmpdir_list, 0, sizeof(mysql_tmpdir_list));

	if (init_tmpdir(&mysql_tmpdir_list, opt_mysql_tmpdir))
		exit(EXIT_FAILURE);

	/* dummy for initialize all_charsets[] */
	get_charset_name(0);

	my_default_lc_messages = my_locale_by_name("en_US");
	init_errmessage();

#ifdef XTRADB_BASED
	srv_page_size = 0;
	srv_page_size_shift = 0;

	if (innobase_page_size != (1 << 14)) {
		int n_shift = get_bit_shift(innobase_page_size);

		if (n_shift >= 12 && n_shift <= UNIV_PAGE_SIZE_SHIFT_MAX) {
			msg("InnoDB: Warning: innodb_page_size has been "
			    "changed from default value 16384.\n");
			srv_page_size_shift = n_shift;
			srv_page_size = 1 << n_shift;
			msg("InnoDB: The universal page size of the "
			    "database is set to %lu.\n", srv_page_size);
		} else {
			msg("InnoDB: Error: invalid value of "
			    "innobase_page_size: %lu", innobase_page_size);
			exit(EXIT_FAILURE);
		}
	} else {
		srv_page_size_shift = 14;
		srv_page_size = (1 << srv_page_size_shift);
	}

	if (!xb_init_log_block_size()) {
		goto error;
	}

	srv_fast_checksum = (ibool) innobase_fast_checksum;
#endif

	/* Check that values don't overflow on 32-bit systems. */
	if (sizeof(ulint) == 4) {
		if (xtrabackup_use_memory > UINT_MAX32) {
			msg("xtrabackup: use-memory can't be over 4GB"
			    " on 32-bit systems\n");
		}

		if (innobase_buffer_pool_size > UINT_MAX32) {
			msg("xtrabackup: innobase_buffer_pool_size can't be "
			    "over 4GB on 32-bit systems\n");

			goto error;
		}

		if (innobase_log_file_size > UINT_MAX32) {
			msg("xtrabackup: innobase_log_file_size can't be "
			    "over 4GB on 32-bit systemsi\n");

			goto error;
		}
	}

  	os_innodb_umask = (ulint)0664;

	/* First calculate the default path for innodb_data_home_dir etc.,
	in case the user has not given any value.

	Note that when using the embedded server, the datadirectory is not
	necessarily the current directory of this program. */

	  	/* It's better to use current lib, to keep paths short */
	  	current_dir[0] = FN_CURLIB;
	  	current_dir[1] = FN_LIBCHAR;
	  	current_dir[2] = 0;
	  	default_path = current_dir;

	ut_a(default_path);

#if (MYSQL_VERSION_ID < 50500)
//	if (specialflag & SPECIAL_NO_PRIOR) {
	        srv_set_thread_priorities = FALSE;
//	} else {
//	        srv_set_thread_priorities = TRUE;
//	        srv_query_thread_priority = QUERY_PRIOR;
//	}
#endif

	/* Set InnoDB initialization parameters according to the values
	read from MySQL .cnf file */

	if (xtrabackup_backup || xtrabackup_stats) {
		msg("xtrabackup: Target instance is assumed as followings.\n");
	} else {
		msg("xtrabackup: Temporary instance for recovery is set as "
		    "followings.\n");
	}

	/*--------------- Data files -------------------------*/

	/* The default dir for data files is the datadir of MySQL */

	srv_data_home = ((xtrabackup_backup || xtrabackup_stats) && innobase_data_home_dir
			 ? innobase_data_home_dir : default_path);
	msg("xtrabackup:   innodb_data_home_dir = %s\n", srv_data_home);

	/* Set default InnoDB data file size to 10 MB and let it be
  	auto-extending. Thus users can use InnoDB in >= 4.0 without having
	to specify any startup options. */

	if (!innobase_data_file_path) {
  		innobase_data_file_path = (char*) "ibdata1:10M:autoextend";
	}
	msg("xtrabackup:   innodb_data_file_path = %s\n",
	    innobase_data_file_path);

	/* Since InnoDB edits the argument in the next call, we make another
	copy of it: */

	internal_innobase_data_file_path = strdup(innobase_data_file_path);

	ret = (my_bool) srv_parse_data_file_paths_and_sizes(
#ifndef INNODB_VERSION_SHORT
				internal_innobase_data_file_path,
				&srv_data_file_names,
				&srv_data_file_sizes,
				&srv_data_file_is_raw_partition,
				&srv_n_data_files,
				&srv_auto_extend_last_data_file,
				&srv_last_file_size_max);
#else
			internal_innobase_data_file_path);
#endif
	if (ret == FALSE) {
	  	msg("xtrabackup: syntax error in innodb_data_file_path\n");
mem_free_and_error:
	  	free(internal_innobase_data_file_path);
                goto error;
	}

	if (xtrabackup_prepare) {
		/* "--prepare" needs filenames only */
		ulint i;

		for (i=0; i < srv_n_data_files; i++) {
			char *p;

			p = srv_data_file_names[i];
			while ((p = strstr(p, SRV_PATH_SEPARATOR_STR)) != NULL)
			{
				p++;
				srv_data_file_names[i] = p;
			}
		}
	}

#ifdef XTRADB_BASED
	srv_doublewrite_file = innobase_doublewrite_file;
#ifndef XTRADB55
	srv_extra_undoslots = (ibool) innobase_extra_undoslots;
#endif
#endif

	/* -------------- Log files ---------------------------*/

	/* The default dir for log files is the datadir of MySQL */

	if (!((xtrabackup_backup || xtrabackup_stats) && INNODB_LOG_DIR)) {
		INNODB_LOG_DIR = default_path;
	}
	if (xtrabackup_prepare && xtrabackup_incremental_dir) {
		INNODB_LOG_DIR = xtrabackup_incremental_dir;
	}
	msg("xtrabackup:   innodb_log_group_home_dir = %s\n", INNODB_LOG_DIR);

#ifdef UNIV_LOG_ARCHIVE
	/* Since innodb_log_arch_dir has no relevance under MySQL,
	starting from 4.0.6 we always set it the same as
	innodb_log_group_home_dir: */

	innobase_log_arch_dir = INNODB_LOG_DIR;

	srv_arch_dir = innobase_log_arch_dir;
#endif /* UNIG_LOG_ARCHIVE */

#if MYSQL_VERSION_ID < 50600
	ret = (my_bool)
#ifndef INNODB_VERSION_SHORT
		srv_parse_log_group_home_dirs(INNODB_LOG_DIR,
						&srv_log_group_home_dirs);
#else
		srv_parse_log_group_home_dirs(INNODB_LOG_DIR);
#endif

	if (ret == FALSE || innobase_mirrored_log_groups != 1) {
		msg("xtrabackup: syntax error in innodb_log_group_home_dir, "
		    "or a wrong number of mirrored log groups\n");

                goto mem_free_and_error;
	}
#else
	srv_normalize_path_for_win(srv_log_group_home_dir);

	if (strchr(srv_log_group_home_dir, ';')
	    || innobase_mirrored_log_groups != 1) {
		msg("syntax error in innodb_log_group_home_dir, "
		    "or a wrong number of mirrored log groups");

		goto mem_free_and_error;
	}
#endif


#ifdef INNODB_VERSION_SHORT
	srv_adaptive_flushing = FALSE;
	srv_use_sys_malloc = TRUE;
	srv_file_format = 1; /* Barracuda */
#if (MYSQL_VERSION_ID < 50500)
	srv_check_file_format_at_startup = UNIV_FORMAT_MAX; /* on */
#else
	srv_max_file_format_at_startup = UNIV_FORMAT_MAX; /* on */
#endif
#endif
	/* --------------------------------------------------*/

	srv_file_flush_method_str = innobase_unix_file_flush_method;

#if MYSQL_VERSION_ID < 50600
	srv_n_log_groups = (ulint) innobase_mirrored_log_groups;
#endif
	srv_n_log_files = (ulint) innobase_log_files_in_group;
	srv_log_file_size = (ulint) innobase_log_file_size;
	msg("xtrabackup:   innodb_log_files_in_group = %ld\n",
	    srv_n_log_files);
	msg("xtrabackup:   innodb_log_file_size = %ld\n",
	    srv_log_file_size);

#ifdef UNIV_LOG_ARCHIVE
	srv_log_archive_on = (ulint) innobase_log_archive;
#endif /* UNIV_LOG_ARCHIVE */
	srv_log_buffer_size = (ulint) innobase_log_buffer_size;

        /* We set srv_pool_size here in units of 1 kB. InnoDB internally
        changes the value so that it becomes the number of database pages. */

#ifndef INNODB_VERSION_SHORT
        if (innobase_buffer_pool_awe_mem_mb == 0) {
                /* Careful here: we first convert the signed long int to ulint
                and only after that divide */

                //srv_pool_size = ((ulint) innobase_buffer_pool_size) / 1024;
		srv_pool_size = ((ulint) xtrabackup_use_memory) / 1024;
        } else {
                srv_use_awe = TRUE;
                srv_pool_size = (ulint)
                                (1024 * innobase_buffer_pool_awe_mem_mb);
                //srv_awe_window_size = (ulint) innobase_buffer_pool_size;
		srv_awe_window_size = (ulint) xtrabackup_use_memory;

                /* Note that what the user specified as
                innodb_buffer_pool_size is actually the AWE memory window
                size in this case, and the real buffer pool size is
                determined by .._awe_mem_mb. */
        }
#else
	//srv_buf_pool_size = (ulint) innobase_buffer_pool_size;
	srv_buf_pool_size = (ulint) xtrabackup_use_memory;
#endif

	srv_mem_pool_size = (ulint) innobase_additional_mem_pool_size;

	srv_n_file_io_threads = (ulint) innobase_file_io_threads;
#ifdef INNODB_VERSION_SHORT
	srv_n_read_io_threads = (ulint) innobase_read_io_threads;
	srv_n_write_io_threads = (ulint) innobase_write_io_threads;
#endif

#ifndef INNODB_VERSION_SHORT
	srv_lock_wait_timeout = (ulint) innobase_lock_wait_timeout;
#endif
	srv_force_recovery = (ulint) innobase_force_recovery;

	srv_use_doublewrite_buf = (ibool) innobase_use_doublewrite;
#if MYSQL_VERSION_ID < 50600
	srv_use_checksums = (ibool) innobase_use_checksums;
#endif

#ifndef INNODB_VERSION_SHORT
	srv_use_adaptive_hash_indexes = (ibool) innobase_adaptive_hash_index;
#else
	btr_search_enabled = (char) innobase_adaptive_hash_index;
#endif

	os_use_large_pages = (ibool) innobase_use_large_pages;
	os_large_page_size = (ulint) innobase_large_page_size;

	row_rollback_on_timeout = (ibool) innobase_rollback_on_timeout;

	srv_file_per_table = (my_bool) innobase_file_per_table;

        srv_locks_unsafe_for_binlog = (ibool) innobase_locks_unsafe_for_binlog;

	srv_max_n_open_files = (ulint) innobase_open_files;
	srv_innodb_status = (ibool) innobase_create_status_file;

	srv_print_verbose_log = 1;

	/* Store the default charset-collation number of this MySQL
	installation */

	/* We cannot treat characterset here for now!! */
	data_mysql_default_charset_coll = (ulint)default_charset_info->number;

	ut_a(DATA_MYSQL_LATIN1_SWEDISH_CHARSET_COLL ==
					my_charset_latin1.number);
	ut_a(DATA_MYSQL_BINARY_CHARSET_COLL == my_charset_bin.number);

	/* Store the latin1_swedish_ci character ordering table to InnoDB. For
	non-latin1_swedish_ci charsets we use the MySQL comparison functions,
	and consequently we do not need to know the ordering internally in
	InnoDB. */

#ifndef INNODB_VERSION_SHORT
	ut_a(0 == strcmp((char*)my_charset_latin1.name,
						(char*)"latin1_swedish_ci"));
	memcpy(srv_latin1_ordering, my_charset_latin1.sort_order, 256);
#else
	ut_a(0 == strcmp(my_charset_latin1.name, "latin1_swedish_ci"));
	srv_latin1_ordering = my_charset_latin1.sort_order;

	//innobase_commit_concurrency_init_default();
#endif

	/* Since we in this module access directly the fields of a trx
        struct, and due to different headers and flags it might happen that
	mutex_t has a different size in this module and in InnoDB
	modules, we check at run time that the size is the same in
	these compilation modules. */

#ifndef INNODB_VERSION_SHORT
	srv_sizeof_trx_t_in_ha_innodb_cc = sizeof(trx_t);
#endif

#if MYSQL_VERSION_ID >= 50500
	/* On 5.5 srv_use_native_aio is TRUE by default. It is later reset
	if it is not supported by the platform in
	innobase_start_or_create_for_mysql(). As we don't call it in xtrabackup,
	we have to duplicate checks from that function here. */

#ifdef __WIN__
	switch (os_get_os_version()) {
	case OS_WIN95:
	case OS_WIN31:
	case OS_WINNT:
		/* On Win 95, 98, ME, Win32 subsystem for Windows 3.1,
		and NT use simulated aio. In NT Windows provides async i/o,
		but when run in conjunction with InnoDB Hot Backup, it seemed
		to corrupt the data files. */

		srv_use_native_aio = FALSE;
		break;

	case OS_WIN2000:
	case OS_WINXP:
		/* On 2000 and XP, async IO is available. */
		srv_use_native_aio = TRUE;
		break;

	default:
		/* Vista and later have both async IO and condition variables */
		srv_use_native_aio = TRUE;
		srv_use_native_conditions = TRUE;
		break;
	}

#elif defined(LINUX_NATIVE_AIO)

	if (srv_use_native_aio) {
		ut_print_timestamp(stderr);
		msg(" InnoDB: Using Linux native AIO\n");
	}
#else
	/* Currently native AIO is supported only on windows and linux
	and that also when the support is compiled in. In all other
	cases, we ignore the setting of innodb_use_native_aio. */
	srv_use_native_aio = FALSE;

#endif
#endif /* MYSQL_VERSION_ID */

	return(FALSE);

error:
	msg("xtrabackup: innodb_init_param(): Error occured.\n");
	return(TRUE);
}

static my_bool
innodb_init(void)
{
	int	err;

#if defined(INNODB_VERSION_SHORT) && MYSQL_VERSION_ID < 50500
	/* InnoDB relies on buf_LRU_old_ratio to be always initialized (see
	debug assertions in buf_LRU_old_adjust_len(). Normally it is initialized
	from ha_innodb.cc. That code is not used in XtraBackup, which led to
	assertion failures in 5.1 debug builds (see LP bug #924492).

	5.5 initializes that variable to a hard-coded value of 37% (and then
	adjusting it to the actual server variable value). That's why the
	assertion failures never occurred on 5.5. Let's do the same for 5.1. */

	buf_LRU_old_ratio_update(100 * 3 / 8, FALSE);
#endif

	err = innobase_start_or_create_for_mysql();

	if (err != DB_SUCCESS) {
	  	free(internal_innobase_data_file_path);
                goto error;
	}

	/* They may not be needed for now */
//	(void) hash_init(&innobase_open_tables,system_charset_info, 32, 0, 0,
//			 		(hash_get_key) innobase_get_key, 0, 0);
//        pthread_mutex_init(&innobase_share_mutex, MY_MUTEX_INIT_FAST);
//        pthread_mutex_init(&prepare_commit_mutex, MY_MUTEX_INIT_FAST);
//        pthread_mutex_init(&commit_threads_m, MY_MUTEX_INIT_FAST);
//        pthread_mutex_init(&commit_cond_m, MY_MUTEX_INIT_FAST);
//        pthread_cond_init(&commit_cond, NULL);

	innodb_inited= 1;

	return(FALSE);

error:
	msg("xtrabackup: innodb_init(): Error occured.\n");
	return(TRUE);
}

static my_bool
innodb_end(void)
{
	srv_fast_shutdown = (ulint) innobase_fast_shutdown;
	innodb_inited = 0;

	msg("xtrabackup: starting shutdown with innodb_fast_shutdown = %lu\n",
	    srv_fast_shutdown);

	if (innobase_shutdown_for_mysql() != DB_SUCCESS) {
		goto error;
	}
	free(internal_innobase_data_file_path);

	/* They may not be needed for now */
//	hash_free(&innobase_open_tables);
//	pthread_mutex_destroy(&innobase_share_mutex);
//	pthread_mutex_destroy(&prepare_commit_mutex);
//	pthread_mutex_destroy(&commit_threads_m);
//	pthread_mutex_destroy(&commit_cond_m);
//	pthread_cond_destroy(&commit_cond);

	return(FALSE);

error:
	msg("xtrabackup: innodb_end(): Error occured.\n");
	return(TRUE);
}

/* ================= common ================= */

/***********************************************************************
Read backup meta info.
@return TRUE on success, FALSE on failure. */
static
my_bool
xtrabackup_read_metadata(char *filename)
{
	FILE *fp;
	my_bool r = TRUE;

	fp = fopen(filename,"r");
	if(!fp) {
		msg("xtrabackup: Error: cannot open %s\n", filename);
		return(FALSE);
	}

	if (fscanf(fp, "backup_type = %29s\n", metadata_type)
	    != 1) {
		r = FALSE;
		goto end;
	}
#ifndef INNODB_VERSION_SHORT
	if (fscanf(fp, "from_lsn = %lu:%lu\n", &metadata_from_lsn.high, &metadata_from_lsn.low)
	    != 2) {
		r = FALSE;
		goto end;
	}
	if (fscanf(fp, "to_lsn = %lu:%lu\n", &metadata_to_lsn.high, &metadata_to_lsn.low)
			!= 2) {
		r = FALSE;
		goto end;
	}
	if (fscanf(fp, "last_lsn = %lu:%lu\n", &metadata_last_lsn.high, &metadata_last_lsn.low)
			!= 2) {
		metadata_last_lsn.high = metadata_last_lsn.low = 0;
	}
#else
	/* Use UINT64PF instead of LSN_PF here, as we have to maintain the file
	format. */
	if (fscanf(fp, "from_lsn = " UINT64PF "\n", &metadata_from_lsn)
			!= 1) {
		r = FALSE;
		goto end;
	}
	if (fscanf(fp, "to_lsn = " UINT64PF "\n", &metadata_to_lsn)
			!= 1) {
		r = FALSE;
		goto end;
	}
	if (fscanf(fp, "last_lsn = " UINT64PF "\n", &metadata_last_lsn)
			!= 1) {
		metadata_last_lsn = 0;
	}
#endif

end:
	fclose(fp);

	return(r);
}

/***********************************************************************
Print backup meta info to a specified buffer. */
static
void
xtrabackup_print_metadata(char *buf, size_t buf_len)
{
#ifndef INNODB_VERSION_SHORT
	snprintf(buf, buf_len,
		 "backup_type = %s\n"
		 "from_lsn = %lu:%lu\n"
		 "to_lsn = %lu:%lu\n"
		 "last_lsn = %lu:%lu\n",
		 metadata_type,
		 metadata_from_lsn.high, metadata_from_lsn.low,
		 metadata_to_lsn.high, metadata_to_lsn.low,
		 metadata_last_lsn.high, metadata_last_lsn.low);
#else
	/* Use UINT64PF instead of LSN_PF here, as we have to maintain the file
	format. */
	snprintf(buf, buf_len,
		 "backup_type = %s\n"
		 "from_lsn = " UINT64PF "\n"
		 "to_lsn = " UINT64PF "\n"
		 "last_lsn = " UINT64PF "\n",
		 metadata_type,
		 metadata_from_lsn,
		 metadata_to_lsn,
		 metadata_last_lsn);
#endif
}

/***********************************************************************
Stream backup meta info to a specified datasink.
@return TRUE on success, FALSE on failure. */
static
my_bool
xtrabackup_stream_metadata(ds_ctxt_t *ds_ctxt)
{
	char		buf[1024];
	size_t		len;
	datasink_t	*ds = ds_ctxt->datasink;
	ds_file_t	*stream;
	MY_STAT		mystat;

	xtrabackup_print_metadata(buf, sizeof(buf));

	len = strlen(buf);

	mystat.st_size = len;
	mystat.st_mtime = my_time(0);

	stream = ds->open(ds_ctxt, XTRABACKUP_METADATA_FILENAME,
			  &mystat);
	if (stream == NULL) {
		msg("xtrabackup: Error: cannot open output stream "
		    "for %s\n", XTRABACKUP_METADATA_FILENAME);
		return(FALSE);
	}

	if (ds->write(stream, buf, len)) {
		ds->close(stream);
		return(FALSE);
	}

	ds->close(stream);

	return(TRUE);
}

/***********************************************************************
Write backup meta info to a specified file.
@return TRUE on success, FALSE on failure. */
static
my_bool
xtrabackup_write_metadata(const char *filepath)
{
	char		buf[1024];
	size_t		len;
	FILE		*fp;

	xtrabackup_print_metadata(buf, sizeof(buf));

	len = strlen(buf);

	fp = fopen(filepath, "w");
	if(!fp) {
		msg("xtrabackup: Error: cannot open %s\n", filepath);
		return(FALSE);
	}
	if (fwrite(buf, len, 1, fp) < 1) {
		fclose(fp);
		return(FALSE);
	}

	fclose(fp);

	return(TRUE);
}

/************************************************************************
Checks if a table specified as a path should be skipped from backup
based on the --tables or --tables-file options.

@return TRUE if the table should be skipped. */
static my_bool
check_if_skip_table(const char *path, const char *suffix)
{
	char buf[FN_REFLEN];
	const char *dbname, *tbname;
	const char *ptr;
	char *eptr;
	int dbname_len;

	if (xtrabackup_tables == NULL && xtrabackup_tables_file == NULL) {
		return(FALSE);
	}

	dbname = NULL;
	tbname = path;
	while ((ptr = strstr(tbname, SRV_PATH_SEPARATOR_STR)) != NULL) {
		dbname = tbname;
		tbname = ptr + 1;
	}

	if (dbname == NULL) {
		return(FALSE);
	}

	strncpy(buf, dbname, FN_REFLEN);
	buf[FN_REFLEN - 1] = 0;
	buf[tbname - 1 - dbname] = '.';

	dbname_len = strlen(dbname) - strlen(suffix);
	if (dbname_len < 1) {
		return(FALSE);
	}
	buf[dbname_len - 1] = 0;

	if ((eptr = strstr(buf, "#P#")) != NULL) {
		*eptr = 0;
	}

	if (xtrabackup_tables) {
		int regres = REG_NOMATCH;
		int i;
		for (i = 0; i < tables_regex_num; i++) {
			regres = xb_regexec(&tables_regex[i], buf, 1,
					    tables_regmatch, 0);
			if (regres != REG_NOMATCH) {
				break;
			}
		}
		if (regres == REG_NOMATCH) {
			return(TRUE);
		}
	}

	if (xtrabackup_tables_file) {
		xtrabackup_tables_t*	table;

		XB_HASH_SEARCH(name_hash, tables_hash, ut_fold_string(buf),
			       table, ut_ad(table->name),
			       !strcmp(table->name, buf));
		if (!table) {
			return(TRUE);
		}
	}

	return(FALSE);
}

/***********************************************************************
Read meta info for an incremental delta.
@return TRUE on success, FALSE on failure. */
static my_bool
xb_read_delta_metadata(const char *filepath, xb_delta_info_t *info)
{
	FILE*	fp;
	char	key[51];
	char	value[51];
	my_bool	r			= TRUE;

	/* set defaults */
	info->page_size = ULINT_UNDEFINED;
	info->zip_size = ULINT_UNDEFINED;
	info->space_id = ULINT_UNDEFINED;

	fp = fopen(filepath, "r");
	if (!fp) {
		/* Meta files for incremental deltas are optional */
		return(TRUE);
	}

	while (!feof(fp)) {
		if (fscanf(fp, "%50s = %50s\n", key, value) == 2) {
			if (strcmp(key, "page_size") == 0) {
				info->page_size = strtoul(value, NULL, 10);
			} else if (strcmp(key, "zip_size") == 0) {
				info->zip_size = strtoul(value, NULL, 10);
			} else if (strcmp(key, "space_id") == 0) {
				info->space_id = strtoul(value, NULL, 10);
			}
		}
	}

	fclose(fp);

	if (info->page_size == ULINT_UNDEFINED) {
		msg("xtrabackup: page_size is required in %s\n", filepath);
		r = FALSE;
	}
	if (info->space_id == ULINT_UNDEFINED) {
		msg("xtrabackup: Warning: This backup was taken with XtraBackup 2.0.1 "
			"or earlier, some DDL operations between full and incremental "
			"backups may be handled incorrectly\n");
	}

	return(r);
}

/***********************************************************************
Write meta info for an incremental delta.
@return TRUE on success, FALSE on failure. */
static
my_bool
xb_write_delta_metadata(ds_ctxt_t *ds_ctxt, const char *filename,
			const xb_delta_info_t *info)
{
	datasink_t	*ds = ds_ctxt->datasink;
	ds_file_t	*f;
	char		buf[64];
	my_bool		ret;
	size_t		len;
	MY_STAT		mystat;

	snprintf(buf, sizeof(buf),
		 "page_size = %lu\n"
		 "zip_size = %lu\n"
		 "space_id = %lu\n",
		 info->page_size, info->zip_size, info->space_id);
	len = strlen(buf);

	mystat.st_size = len;
	mystat.st_mtime = my_time(0);

	f = ds->open(ds_ctxt, filename, &mystat);
	if (f == NULL) {
		msg("xtrabackup: Error: cannot open output stream for %s\n",
		    filename);
		return(FALSE);
	}

	ret = ds->write(f, buf, len) == 0;

	ds->close(f);

	return(ret);
}

/* ================= backup ================= */
static void
xtrabackup_io_throttling(void)
{
	if (xtrabackup_throttle && (io_ticket--) < 0) {
		os_event_reset(wait_throttle);
		os_event_wait(wait_throttle);
	}
}

#ifndef XTRADB_BASED
#define trx_sys_sys_space(id) (id == 0)
#endif

/****************************************************************//**
A simple function to open or create a file.
@return own: handle to the file, not defined if error, error number
can be retrieved with os_file_get_last_error */
UNIV_INLINE
os_file_t
xb_file_create_no_error_handling(
/*=============================*/
	const char*	name,	/*!< in: name of the file or path as a
				null-terminated string */
	ulint		create_mode,/*!< in: OS_FILE_OPEN if an existing file
				is opened (if does not exist, error), or
				OS_FILE_CREATE if a new file is created
				(if exists, error) */
	ulint		access_type,/*!< in: OS_FILE_READ_ONLY,
				OS_FILE_READ_WRITE, or
				OS_FILE_READ_ALLOW_DELETE; the last option is
				used by a backup program reading the file */
	ibool*		success)/*!< out: TRUE if succeed, FALSE if error */
{
#if MYSQL_VERSION_ID > 50500
	return os_file_create_simple_no_error_handling(
		0, /* innodb_file_data_key */
		name, create_mode, access_type, success);
#else
	return os_file_create_simple_no_error_handling(
		name, create_mode, access_type, success);
#endif
}

/****************************************************************//**
Opens an existing file or creates a new.
@return own: handle to the file, not defined if error, error number
can be retrieved with os_file_get_last_error */
UNIV_INLINE
os_file_t
xb_file_create(
/*===========*/
	const char*	name,	/*!< in: name of the file or path as a
				null-terminated string */
	ulint		create_mode,/*!< in: OS_FILE_OPEN if an existing file
				is opened (if does not exist, error), or
				OS_FILE_CREATE if a new file is created
				(if exists, error),
				OS_FILE_OVERWRITE if a new file is created
				or an old overwritten;
				OS_FILE_OPEN_RAW, if a raw device or disk
				partition should be opened */
	ulint		purpose,/*!< in: OS_FILE_AIO, if asynchronous,
				non-buffered i/o is desired,
				OS_FILE_NORMAL, if any normal file;
				NOTE that it also depends on type, os_aio_..
				and srv_.. variables whether we really use
				async i/o or unbuffered i/o: look in the
				function source code for the exact rules */
	ulint		type,	/*!< in: OS_DATA_FILE or OS_LOG_FILE */
	ibool*		success)/*!< out: TRUE if succeed, FALSE if error */
{
	os_file_t	result;
#if MYSQL_VERSION_ID >= 50600
	ibool	old_srv_read_only_mode = srv_read_only_mode;

	srv_read_only_mode = FALSE;
#endif
#if MYSQL_VERSION_ID > 50500
	result = os_file_create(0 /* innodb_file_data_key */,
				name, create_mode, purpose, type, success);
#else
	result = os_file_create(name, create_mode, purpose, type, success);
#endif
#if MYSQL_VERSION_ID >= 50600
	srv_read_only_mode = old_srv_read_only_mode;
#endif
	return result;
}

/***********************************************************************//**
Renames a file (can also move it to another directory). It is safest that the
file is closed before calling this function.
@return	TRUE if success */
UNIV_INLINE
ibool
xb_file_rename(
/*===========*/
	const char*	oldpath,/*!< in: old file path as a null-terminated
				string */
	const char*	newpath)/*!< in: new file path */
{
#if MYSQL_VERSION_ID > 50500
	return os_file_rename(
		0 /* innodb_file_data_key */, oldpath, newpath);
#else
	return os_file_rename(oldpath, newpath);
#endif
}

UNIV_INLINE
void
xb_file_set_nocache(
/*================*/
	os_file_t	fd,		/* in: file descriptor to alter */
	const char*	file_name,	/* in: used in the diagnostic message */
	const char*	operation_name) /* in: used in the diagnostic message,
					we call os_file_set_nocache()
					immediately after opening or creating
					a file, so this is either "open" or
					"create" */
{
#ifndef __WIN__
	if (srv_unix_file_flush_method == SRV_UNIX_O_DIRECT ||
		srv_unix_file_flush_method == SRV_UNIX_O_DIRECT_NO_FSYNC) {
		os_file_set_nocache(fd, file_name, operation_name);
	}
#endif
}

#ifdef INNODB_VERSION_SHORT
/***********************************************************************
Reads the space flags from a given data file and returns the compressed
page size, or 0 if the space is not compressed. */
static
ulint
xb_get_zip_size(os_file_t file)
{
	byte	*buf;
	byte	*page;
	ulint	 zip_size = ULINT_UNDEFINED;
	ibool	 success;
	ulint	 space;

	buf = static_cast<byte *>(ut_malloc(2 * UNIV_PAGE_SIZE_MAX));
	page = static_cast<byte *>(ut_align(buf, UNIV_PAGE_SIZE_MAX));

	success = xb_os_file_read(file, page, 0, UNIV_PAGE_SIZE_MAX);
	if (!success) {
		goto end;
	}

	space = mach_read_from_4(page + FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID);
	zip_size = (space == 0 ) ? 0 :
		dict_tf_get_zip_size(fsp_header_get_flags(page));
end:
	ut_free(buf);

	return(zip_size);
}
#endif

/* TODO: We may tune the behavior (e.g. by fil_aio)*/
#define COPY_CHUNK 64

static
my_bool
xtrabackup_copy_datafile(fil_node_t* node, uint thread_n, ds_ctxt_t *ds_ctxt)
{
	os_file_t	src_file = XB_FILE_UNDEFINED;
	MY_STAT		src_stat;
	char		dst_name[FN_REFLEN];
	char		meta_name[FN_REFLEN];
	ibool		success;
	byte*		page;
	byte*		buf2 = NULL;
	IB_UINT64	file_size;
	IB_UINT64	offset;
	ulint		page_in_buffer;
	ulint		incremental_buffers = 0;
	byte*		incremental_buffer = NULL;
	byte*		incremental_buffer_base = NULL;
	ulint		page_size;
	ulint		page_size_shift;
	xb_delta_info_t info;
	datasink_t	*ds = ds_ctxt->datasink;
	ds_file_t	*dstfile = NULL;

	info.page_size = 0;
	info.zip_size = 0;
	info.space_id = 0;

	if ((!trx_sys_sys_space(node->space->id))
		&& check_if_skip_table(node->name, "ibd")) {
		printf("[%02u] Skipping %s.\n",
		       thread_n, node->name);
		return(FALSE);
	}

	if (trx_sys_sys_space(node->space->id))
	{
		char *next, *p;
		/* system datafile "/fullpath/datafilename.ibd" or "./datafilename.ibd" */
		p = node->name;
		while ((next = strstr(p, SRV_PATH_SEPARATOR_STR)) != NULL)
		{
			p = next + 1;
		}
		strncpy(dst_name, p, sizeof(dst_name));
	} else {
		/* file per table style "./database/table.ibd" */
		strncpy(dst_name, node->name, sizeof(dst_name));
	}

	/* open src_file*/
	if (!node->open) {
		src_file = xb_file_create_no_error_handling(node->name,
							    OS_FILE_OPEN,
							    OS_FILE_READ_ONLY,
							    &success);
		if (!success) {
			/* The following call prints an error message */
			os_file_get_last_error(TRUE);

			msg("[%02u] xtrabackup: Warning: cannot open %s\n"
			    "[%02u] xtrabackup: Warning: We assume the "
			    "table was dropped or renamed during "
			    "xtrabackup execution and ignore the file.\n",
			    thread_n, node->name, thread_n);
			goto skip;
		}

		xb_file_set_nocache(src_file, node->name, "OPEN");
	} else {
		src_file = node->handle;
	}

#ifdef USE_POSIX_FADVISE
	posix_fadvise(src_file, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

#ifndef INNODB_VERSION_SHORT
	page_size = UNIV_PAGE_SIZE;
	page_size_shift = UNIV_PAGE_SIZE_SHIFT;
#else
	info.zip_size = xb_get_zip_size(src_file);
	if (info.zip_size == ULINT_UNDEFINED) {
		goto skip;
	} else if (info.zip_size) {
		page_size = info.zip_size;
		page_size_shift = get_bit_shift(page_size);
		msg("[%02u] %s is compressed with page size = "
		    "%lu bytes\n", thread_n, node->name, page_size);
		if (page_size_shift < 10 || page_size_shift > 14) {
			msg("[%02u] xtrabackup: Error: Invalid "
			    "page size: %lu.\n", thread_n, page_size);
			ut_error;
		}
	} else {
		page_size = UNIV_PAGE_SIZE;
		page_size_shift = UNIV_PAGE_SIZE_SHIFT;
	}
#endif

	if (xtrabackup_incremental) {
		/* allocate buffer for incremental backup (4096 pages) */
		incremental_buffer_base
			= static_cast<byte *>(ut_malloc((UNIV_PAGE_SIZE_MAX
							 / 4 + 1)
							* UNIV_PAGE_SIZE_MAX));
		incremental_buffer
			= static_cast<byte *>(ut_align(incremental_buffer_base,
						       UNIV_PAGE_SIZE_MAX));

		snprintf(meta_name, sizeof(meta_name),
			 "%s%s", dst_name, XB_DELTA_INFO_SUFFIX);
		strcat(dst_name, ".delta");

		/* clear buffer */
		memset(incremental_buffer, 0, (page_size/4) * page_size);
		page_in_buffer = 0;
		mach_write_to_4(incremental_buffer, 0x78747261UL);/*"xtra"*/
		page_in_buffer++;

		info.page_size = page_size;
		info.space_id = node->space->id;
	} else
		info.page_size = 0;

	if (my_stat(node->name, &src_stat, MYF(MY_WME)) == NULL) {
		msg("[%02u] xtrabackup: Warning: cannot stat %s\n",
		    thread_n, node->name);
		goto skip;
	}
	dstfile = ds->open(ds_ctxt, dst_name, &src_stat);
	if (dstfile == NULL) {
		msg("[%02u] xtrabackup: error: "
		    "cannot open the destination stream for %s\n",
		    thread_n, dst_name);
		goto error;
	}

	if (xtrabackup_stream) {
		const char *action = xtrabackup_compress ?
			"Compressing and streaming" : "Streaming";
		msg("[%02u] %s %s\n", thread_n, action, node->name);
	} else {
		const char *action;

		if (xtrabackup_compress) {
			action = "Compressing";
		} else {
			action = "Copying";
		}
		msg("[%02u] %s %s to %s\n", thread_n, action,
		    node->name, dstfile->path);
	}

	buf2 = static_cast<byte *>(ut_malloc(COPY_CHUNK
					     * page_size + UNIV_PAGE_SIZE));
	page = static_cast<byte *>(ut_align(buf2, UNIV_PAGE_SIZE));

	success = xb_os_file_read(src_file, page, 0, UNIV_PAGE_SIZE);
	if (!success) {
		goto error;
	}

	file_size = src_stat.st_size;;

	for (offset = 0; offset < file_size; offset += COPY_CHUNK * page_size) {
		ulint chunk;
		ulint chunk_offset;
		ulint retry_count = 10;

		if (file_size - offset > COPY_CHUNK * page_size) {
			chunk = COPY_CHUNK * page_size;
		} else {
			chunk = (ulint)(file_size - offset);
		}

read_retry:
		xtrabackup_io_throttling();

		success = xb_os_file_read(src_file, page, offset, chunk);
		if (!success) {
			goto error;
		}

#ifdef USE_POSIX_FADVISE
		posix_fadvise(src_file, 0, 0, POSIX_FADV_DONTNEED);
#endif

		/* check corruption and retry */
		for (chunk_offset = 0; chunk_offset < chunk; chunk_offset += page_size) {
			if (xb_buf_page_is_corrupted(page + chunk_offset,
						     info.zip_size))
			{
				if (
				    trx_sys_sys_space(node->space->id)
				    && ((offset + (IB_INT64)chunk_offset) >> page_size_shift)
				       >= FSP_EXTENT_SIZE
				    && ((offset + (IB_INT64)chunk_offset) >> page_size_shift)
				       < FSP_EXTENT_SIZE * 3) {
					/* double write buffer may have old data in the end
					   or it may contain the other format page like COMPRESSED.
 					   So, we can pass the check of double write buffer.*/
					ut_a(page_size == UNIV_PAGE_SIZE);
					msg("[%02u] xtrabackup: "
					    "Page %lu seems double write "
					    "buffer. passing the check.\n",
					    thread_n,
					    (ulint)((offset +
						     (IB_INT64)chunk_offset) >>
						    page_size_shift));
				} else {
					retry_count--;
					if (retry_count == 0) {
						msg("[%02u] xtrabackup: "
						    "Error: 10 retries "
						    "resulted in fail. File "
						    "%s seems to be "
						    "corrupted.\n",
						    thread_n, node->name);
						goto error;
					}
					msg("[%02u] xtrabackup: "
					    "Database page corruption "
					    "detected at page %lu. "
					    "retrying...\n",
					    thread_n,
					    (ulint)((offset +
						     (IB_INT64)chunk_offset)
						    >> page_size_shift));

					os_thread_sleep(100000);

					goto read_retry;
				}
			}
		}

		if (xtrabackup_incremental) {
			for (chunk_offset = 0; chunk_offset < chunk; chunk_offset += page_size) {
				/* newer page */
				/* This condition may be OK for header, ibuf
				and fsp. */
				if (ut_dulint_cmp(incremental_lsn,
					MACH_READ_64(page + chunk_offset
						     + FIL_PAGE_LSN)) < 0) {
	/* ========================================= */
	IB_INT64 offset_on_page;

	if (page_in_buffer == page_size/4) {
		/* flush buffer */
		if (ds->write(dstfile, incremental_buffer,
			      page_in_buffer * page_size)) {
			goto error;
		}

		incremental_buffers++;

		/* clear buffer */
		memset(incremental_buffer, 0, (page_size/4) * page_size);
		page_in_buffer = 0;
		mach_write_to_4(incremental_buffer, 0x78747261UL);/*"xtra"*/
		page_in_buffer++;
	}

	offset_on_page = ((offset + (IB_INT64)chunk_offset) >> page_size_shift);
	ut_a(offset_on_page >> 32 == 0);

	mach_write_to_4(incremental_buffer + page_in_buffer * 4, (ulint)offset_on_page);
	memcpy(incremental_buffer + page_in_buffer * page_size,
	       page + chunk_offset, page_size);

	page_in_buffer++;
	/* ========================================= */
				}
			}
		} else {
			if (ds->write(dstfile, page, chunk)) {
				goto error;
			}
		}

	}

	if (xtrabackup_incremental) {
		/* termination */
		if (page_in_buffer != page_size/4) {
			mach_write_to_4(incremental_buffer + page_in_buffer * 4, 0xFFFFFFFFUL);
		}

		mach_write_to_4(incremental_buffer, 0x58545241UL);/*"XTRA"*/

		/* flush buffer */
		if (ds->write(dstfile, incremental_buffer,
			      page_in_buffer * page_size)) {
			goto error;
		}
		if (!xb_write_delta_metadata(ds_ctxt, meta_name, &info)) {
			msg("[%02u] xtrabackup: Error: "
			    "failed to write meta info for %s\n",
			    thread_n, dst_name);
			goto error;
		}
	}

	/* TODO: How should we treat double_write_buffer here? */
	/* (currently, don't care about. Because,
	    the blocks is newer than the last checkpoint anyway.) */

	/* close */
	msg("[%02u]        ...done\n", thread_n);
	if (!node->open) {
		os_file_close(src_file);
	}
	ds->close(dstfile);
	if (incremental_buffer_base)
		ut_free(incremental_buffer_base);
	ut_free(buf2);
	return(FALSE);
error:
	if (src_file != XB_FILE_UNDEFINED && !node->open)
		os_file_close(src_file);
	if (dstfile != NULL)
		ds->close(dstfile);
	if (incremental_buffer_base)
		ut_free(incremental_buffer_base);
	if (buf2)
		ut_free(buf2);
	msg("[%02u] xtrabackup: Error: "
	    "xtrabackup_copy_datafile() failed.\n", thread_n);
	return(TRUE); /*ERROR*/

skip:
	if (src_file != XB_FILE_UNDEFINED && !node->open)
		os_file_close(src_file);
	if (dstfile != NULL)
		ds->close(dstfile);
	if (incremental_buffer_base)
		ut_free(incremental_buffer_base);
	if (buf2)
		ut_free(buf2);
	msg("[%02u] xtrabackup: Warning: We assume the "
	    "table was dropped during xtrabackup execution "
	    "and ignore the file.\n", thread_n);
	msg("[%02u] xtrabackup: Warning: skipping file %s.\n",
	    thread_n, node->name);
	return(FALSE);
}

static my_bool
xtrabackup_copy_logfile(lsn_t from_lsn, my_bool is_last)
{
	/* definition from recv_recovery_from_checkpoint_start() */
	log_group_t*	group;
	lsn_t		group_scanned_lsn;
	lsn_t		contiguous_lsn;

	ut_a(dst_log_fd != XB_FILE_UNDEFINED);

	/* read from checkpoint_lsn_start to current */
	contiguous_lsn = ut_dulint_align_down(from_lsn,
						OS_FILE_LOG_BLOCK_SIZE);

	/* TODO: We must check the contiguous_lsn still exists in log file.. */

	group = UT_LIST_GET_FIRST(log_sys->log_groups);

	while (group) {
		ibool	finished;
		lsn_t	start_lsn;
		lsn_t	end_lsn;

		/* reference recv_group_scan_log_recs() */
	finished = FALSE;

	start_lsn = contiguous_lsn;
		
	while (!finished) {			
		end_lsn = ut_dulint_add(start_lsn, RECV_SCAN_SIZE);

		xtrabackup_io_throttling();

		mutex_enter(&log_sys->mutex);

		log_group_read_log_seg(LOG_RECOVER, log_sys->buf,
						group, start_lsn, end_lsn);


		/* reference recv_scan_log_recs() */
		{
	byte*	log_block;
	lsn_t	scanned_lsn;
	ulint	data_len;

	ulint	scanned_checkpoint_no = 0;

	finished = FALSE;
	
	log_block = log_sys->buf;
	scanned_lsn = start_lsn;

	while (log_block < log_sys->buf + RECV_SCAN_SIZE && !finished) {
		ulint	no = log_block_get_hdr_no(log_block);
		ulint	scanned_no = log_block_convert_lsn_to_no(scanned_lsn);
		ibool	checksum_is_ok =
			log_block_checksum_is_ok_or_old_format(log_block);

		if (no != scanned_no && checksum_is_ok) {
			ulint blocks_in_group;

			if (no < scanned_no) {
				/* incompletely written log block, do nothing */
				finished = TRUE;
				break;
			}

			blocks_in_group = log_block_convert_lsn_to_no(
#ifndef INNODB_VERSION_SHORT
				ut_dulint_create(0,
						 log_group_get_capacity(group))
#else
				log_group_get_capacity(group)
#endif
							    ) - 1;

			msg("xtrabackup: error:"
			    " log block numbers mismatch:\n"
			    "xtrabackup: error: expected log block no. %lu,"
			    " but got no. %lu from the log file.\n",
			    (ulong) scanned_no, (ulong) no);

			if ((no - scanned_no) % blocks_in_group == 0) {
				msg("xtrabackup: error:"
				    " it looks like InnoDB log has wrapped"
				    " around before xtrabackup could"
				    " process all records due to either"
				    " log copying being too slow, or "
				    " log files being too small.\n");
			}

			goto error;
		} else if (!checksum_is_ok) {
			/* Garbage or an incompletely written log block */

			msg("xtrabackup: warning: Log block checksum mismatch"
#ifndef INNODB_VERSION_SHORT
			    " (block no %lu at lsn %lu %lu): \n"
#else
			    " (block no %lu at lsn " LSN_PF "): \n"
#endif
			    "expected %lu, calculated checksum %lu\n",
				(ulong) no,
#ifndef INNODB_VERSION_SHORT
				(ulong) ut_dulint_get_high(scanned_lsn),
				(ulong) ut_dulint_get_low(scanned_lsn),
#else
				scanned_lsn,
#endif
				(ulong) log_block_get_checksum(log_block),
				(ulong) log_block_calc_checksum(log_block));
			msg("xtrabackup: warning: this is possible when the "
			    "log block has not been fully written by the "
			    "server, will retry later.\n");
			finished = TRUE;
			break;
		}

		if (log_block_get_flush_bit(log_block)) {
			/* This block was a start of a log flush operation:
			we know that the previous flush operation must have
			been completed for all log groups before this block
			can have been flushed to any of the groups. Therefore,
			we know that log data is contiguous up to scanned_lsn
			in all non-corrupt log groups. */

			if (ut_dulint_cmp(scanned_lsn, contiguous_lsn) > 0) {
				contiguous_lsn = scanned_lsn;
			}
		}

		data_len = log_block_get_data_len(log_block);

		if (
		    (scanned_checkpoint_no > 0)
		    && (log_block_get_checkpoint_no(log_block)
		       < scanned_checkpoint_no)
		    && (scanned_checkpoint_no
			- log_block_get_checkpoint_no(log_block)
			> 0x80000000UL)) {

			/* Garbage from a log buffer flush which was made
			before the most recent database recovery */

			finished = TRUE;
			break;
		}		    

		scanned_lsn = ut_dulint_add(scanned_lsn, data_len);
		scanned_checkpoint_no = log_block_get_checkpoint_no(log_block);

		if (data_len < OS_FILE_LOG_BLOCK_SIZE) {
			/* Log data for this group ends here */

			finished = TRUE;
		} else {
			log_block += OS_FILE_LOG_BLOCK_SIZE;
		}
	} /* while (log_block < log_sys->buf + RECV_SCAN_SIZE && !finished) */

	group_scanned_lsn = scanned_lsn;



		}

		/* ===== write log to 'xtrabackup_logfile' ====== */
		{
		ulint 	write_size;
		size_t  rc = 0;

		if (!finished) {
			write_size = RECV_SCAN_SIZE;
		} else {
			write_size = (ulint) ut_dulint_minus(
					ut_dulint_align_up(group_scanned_lsn, OS_FILE_LOG_BLOCK_SIZE),
					start_lsn);
			if (!is_last &&
#ifndef INNODB_VERSION_SHORT
			    group_scanned_lsn.low % OS_FILE_LOG_BLOCK_SIZE
#else
			    group_scanned_lsn % OS_FILE_LOG_BLOCK_SIZE
#endif
			    )
				write_size -= OS_FILE_LOG_BLOCK_SIZE;
		}


		rc = my_write(dst_log_fd, log_sys->buf, write_size,
			      MYF(MY_WME | MY_NABP));

#ifdef USE_POSIX_FADVISE
		if (!xtrabackup_log_only) {
			posix_fadvise(dst_log_fd, 0, 0, POSIX_FADV_DONTNEED);
		}
#endif

		if(rc) {
			msg("xtrabackup: Error: write to logfile failed\n");
			goto error;
		}


		}

		mutex_exit(&log_sys->mutex);

		start_lsn = end_lsn;
	}



		group->scanned_lsn = group_scanned_lsn;

#ifndef INNODB_VERSION_SHORT
		msg(">> log scanned up to (%lu %lu)\n",
		    group->scanned_lsn.high, group->scanned_lsn.low);
#else
		msg(">> log scanned up to (" LSN_PF ")\n",
		    group->scanned_lsn);
#endif

		group = UT_LIST_GET_NEXT(log_groups, group);

		/* update global variable*/
		log_copy_scanned_lsn = group_scanned_lsn;

		/* innodb_mirrored_log_groups must be 1, no other groups */
		ut_a(group == NULL);

		debug_sync_point("xtrabackup_copy_logfile_pause");

	}


	return(FALSE);

error:
	mutex_exit(&log_sys->mutex);
	my_close(dst_log_fd, MYF(MY_WME));
	msg("xtrabackup: Error: xtrabackup_copy_logfile() failed.\n");
	return(TRUE);
}

/* copying logfile in background */
#define SLEEPING_PERIOD 5

static
#ifndef __WIN__
void*
#else
ulint
#endif
log_copying_thread(
	void*	arg __attribute__((unused)))
{
	ulint	counter = 0;

	ut_a(dst_log_fd != XB_FILE_UNDEFINED);

	log_copying_running = TRUE;

	while(log_copying) {
		os_thread_sleep(200000); /*0.2 sec*/

		counter++;
		if(counter >= SLEEPING_PERIOD * 5) {
			if(xtrabackup_copy_logfile(log_copy_scanned_lsn, FALSE))
				goto end;
			counter = 0;
		}
	}

	/* last copying */
	if(xtrabackup_copy_logfile(log_copy_scanned_lsn, TRUE))
		goto end;

	log_copying_succeed = TRUE;
end:
	log_copying_running = FALSE;
	os_thread_exit(NULL);

	return(0);
}

/* io throttle watching (rough) */
static
#ifndef __WIN__
void*
#else
ulint
#endif
io_watching_thread(
	void*	arg)
{
	(void)arg;
	/* currently, for --backup only */
	ut_a(xtrabackup_backup);

	while (log_copying) {
		os_thread_sleep(1000000); /*1 sec*/

		//for DEBUG
		//if (io_ticket == xtrabackup_throttle) {
		//	msg("There seem to be no IO...?\n");
		//}

		io_ticket = xtrabackup_throttle;
		os_event_set(wait_throttle);
	}

	/* stop io throttle */
	xtrabackup_throttle = 0;
	os_event_set(wait_throttle);

	os_thread_exit(NULL);

	return(0);
}

/************************************************************************
I/o-handler thread function. */
static

#ifndef __WIN__
void*
#else
ulint
#endif
io_handler_thread(
/*==============*/
	void*	arg)
{
	ulint	segment;
	
	segment = *((ulint*)arg);

	while (srv_shutdown_state != SRV_SHUTDOWN_EXIT_THREADS) {
		fil_aio_wait(segment);
	}

	/* We count the number of threads in os_thread_exit(). A created
	thread should always use that to exit and not use return() to exit.
	The thread actually never comes here because it is exited in an
	os_event_wait(). */

	os_thread_exit(NULL);

#ifndef __WIN__
	return(NULL);				/* Not reached */
#else
	return(0);
#endif
}

#define SRV_N_PENDING_IOS_PER_THREAD 	OS_AIO_N_PENDING_IOS_PER_THREAD
#define SRV_MAX_N_PENDING_SYNC_IOS	100

/************************************************************************
Initialize the tablespace memory cache and populate it by scanning for and
opening data files.
@returns DB_SUCCESS or error code.*/
static
ulint
xb_data_files_init(void)
/*====================*/
{
	ulint	i;
	ibool	create_new_db;
#ifdef XTRADB_BASED
	ibool	create_new_doublewrite_file;
#endif
	ulint	err;
	lsn_t	min_flushed_lsn;
	lsn_t	max_flushed_lsn;
	ulint   sum_of_new_sizes;

#ifndef INNODB_VERSION_SHORT
	os_aio_init(8 * SRV_N_PENDING_IOS_PER_THREAD
		    * srv_n_file_io_threads,
		    srv_n_file_io_threads,
		    SRV_MAX_N_PENDING_SYNC_IOS);

	fil_init(srv_max_n_open_files);
#else
#if MYSQL_VERSION_ID >= 50600
	srv_n_file_io_threads = srv_n_read_io_threads;
#else
	srv_n_file_io_threads = 2 + srv_n_read_io_threads +
		srv_n_write_io_threads;
#endif

	os_aio_init(8 * SRV_N_PENDING_IOS_PER_THREAD,
		    srv_n_read_io_threads,
		    srv_n_write_io_threads,
		    SRV_MAX_N_PENDING_SYNC_IOS);

	fil_init(srv_file_per_table ? 50000 : 5000,
		 srv_max_n_open_files);
#endif

	fsp_init();

	for (i = 0; i < srv_n_file_io_threads; i++) {
		thread_nr[i] = i;

		os_thread_create(io_handler_thread, thread_nr + i,
				 thread_ids + i);
    	}

	os_thread_sleep(200000); /*0.2 sec*/

	err = open_or_create_data_files(&create_new_db,
#ifdef XTRADB_BASED
					&create_new_doublewrite_file,
#endif
					&min_flushed_lsn, &max_flushed_lsn,
					&sum_of_new_sizes);
	if (err != DB_SUCCESS) {
		msg("xtrabackup: Could not open or create data files.\n"
		    "xtrabackup: If you tried to add new data files, and it "
		    "failed here,\n"
		    "xtrabackup: you should now edit innodb_data_file_path in "
		    "my.cnf back\n"
		    "xtrabackup: to what it was, and remove the new ibdata "
		    "files InnoDB created\n"
		    "xtrabackup: in this failed attempt. InnoDB only wrote "
		    "those files full of\n"
		    "xtrabackup: zeros, but did not yet use them in any way. "
		    "But be careful: do not\n"
		    "xtrabackup: remove old data files which contain your "
		    "precious data!\n");
		return(err);
	}

	/* create_new_db must not be TRUE.. */
	if (create_new_db) {
		msg("xtrabackup: could not find data files at the "
		    "specified datadir\n");
		return(DB_ERROR);
	}

	return(fil_load_single_table_tablespaces());
}

/*********************************************************************//**
Normalizes init parameter values to use units we use inside InnoDB.
@return	DB_SUCCESS or error code */
static
void
xb_normalize_init_values(void)
/*==========================*/
{
	ulint	i;

	for (i = 0; i < srv_n_data_files; i++) {
		srv_data_file_sizes[i] = srv_data_file_sizes[i]
					* ((1024 * 1024) / UNIV_PAGE_SIZE);
	}

	srv_last_file_size_max = srv_last_file_size_max
					* ((1024 * 1024) / UNIV_PAGE_SIZE);

	srv_log_file_size = srv_log_file_size / UNIV_PAGE_SIZE;

	srv_log_buffer_size = srv_log_buffer_size / UNIV_PAGE_SIZE;

#ifndef INNODB_VERSION_SHORT
	srv_pool_size = srv_pool_size / (UNIV_PAGE_SIZE / 1024);

	srv_awe_window_size = srv_awe_window_size / UNIV_PAGE_SIZE;

	if (srv_use_awe) {
	        /* If we are using AWE we must save memory in the 32-bit
		address space of the process, and cannot bind the lock
		table size to the real buffer pool size. */

	        srv_lock_table_size = 20 * srv_awe_window_size;
	} else {
	        srv_lock_table_size = 5 * srv_pool_size;
	}
#else
	srv_lock_table_size = 5 * (srv_buf_pool_size / UNIV_PAGE_SIZE);
#endif
}

/************************************************************************
Destroy the tablespace memory cache. */
static
void
xb_data_files_close(void)
/*====================*/
{
	ulint	i;

	/* Shutdown the aio threads. This has been copied from
	innobase_shutdown_for_mysql(). */

	srv_shutdown_state = SRV_SHUTDOWN_EXIT_THREADS;

	for (i = 0; i < 1000; i++) {
		os_aio_wake_all_threads_at_shutdown();

		os_mutex_enter(os_sync_mutex);

		if (os_thread_count == 0) {

			os_mutex_exit(os_sync_mutex);

			os_thread_sleep(10000);

			break;
		}

		os_mutex_exit(os_sync_mutex);

		os_thread_sleep(10000);
	}

	if (i == 1000) {
		msg("xtrabackup: Warning: %lu threads created by InnoDB"
		    " had not exited at shutdown!\n",
		    (ulong) os_thread_count);
	}

#ifdef INNODB_VERSION_SHORT
	os_aio_free();
#endif
	fil_close_all_files();
#ifndef INNODB_VERSION_SHORT
	fil_free_all_spaces();
#endif
	fil_system = NULL;

	/* Reset srv_file_io_threads to its default value to avoid confusing
	warning on --prepare in innobase_start_or_create_for_mysql()*/
	srv_n_file_io_threads = 4;

	srv_shutdown_state = SRV_SHUTDOWN_NONE;
}

/**************************************************************************
Datafiles copying thread.*/
static
os_thread_ret_t
data_copy_thread_func(
/*==================*/
	void *arg) /* thread context */
{
	data_thread_ctxt_t	*ctxt = (data_thread_ctxt_t *) arg;
	uint			num = ctxt->num;
	fil_node_t*     	node;

	/*
	  Initialize mysys thread-specific memory so we can
	  use mysys functions in this thread.
	*/
	my_thread_init();

	debug_sync_point("data_copy_thread_func");

	while ((node = datafiles_iter_next(ctxt->it)) != NULL) {

		/* copy the datafile */
		if(xtrabackup_copy_datafile(node, num, ctxt->ds_ctxt)) {
			msg("[%02u] xtrabackup: Error: "
			    "failed to copy datafile.\n", num);
			exit(EXIT_FAILURE);
		}
	}

	os_mutex_enter(ctxt->count_mutex);
	(*ctxt->count)--;
	os_mutex_exit(ctxt->count_mutex);

	my_thread_end();
	os_thread_exit(NULL);
	OS_THREAD_DUMMY_RETURN;
}

/***********************************************************************
Stream the transaction log from a temporary file a specified datasink.
@return FALSE on succees, TRUE on error. */
static
ibool
xtrabackup_stream_temp_logfile(File src_file, ds_ctxt_t *ds_ctxt)
{
	datasink_t	*ds = ds_ctxt->datasink;
	uchar		*buf = NULL;
	const size_t	buf_size = 1024 * 1024;
	size_t		bytes;
	ds_file_t	*dst_file = NULL;
	MY_STAT		mystat;

	msg("xtrabackup: Streaming transaction log from a temporary file...\n");

#ifdef USE_POSIX_FADVISE
	posix_fadvise(src_file, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

	if (my_seek(src_file, 0, SEEK_SET, MYF(0)) == MY_FILEPOS_ERROR) {
		msg("xtrabackup: error: my_seek() failed, errno = %d.\n",
		    my_errno);
		    goto err;
	}

	if (my_fstat(src_file, &mystat, MYF(0))) {
		msg("xtrabackup: error: my_fstat() failed.\n");
		goto err;
	}

	dst_file = ds->open(ds_ctxt, XB_LOG_FILENAME, &mystat);
	if (dst_file == NULL) {
		msg("xtrabackup: error: cannot open the destination stream "
		    "for %s.\n", XB_LOG_FILENAME);
		goto err;
	}

	buf = (uchar *) ut_malloc(buf_size);

	while ((bytes = my_read(src_file, buf, buf_size, MYF(MY_WME))) > 0) {
#ifdef USE_POSIX_FADVISE
		posix_fadvise(src_file, 0, 0, POSIX_FADV_DONTNEED);
#endif
		if (ds->write(dst_file, buf, bytes)) {
			msg("xtrabackup: error: cannot write to stream "
			    "for %s.\n", XB_LOG_FILENAME);
			goto err;
		}
	}
	if (bytes == (size_t) -1) {
		goto err;
	}

	ut_free(buf);
	ds->close(dst_file);
	my_close(src_file, MYF(MY_WME));

	msg("xtrabackup: Done.\n");

	return(FALSE);

err:
	if (buf)
		ut_free(buf);
	if (dst_file)
		ds->close(dst_file);;
	if (src_file >= 0)
		my_close(src_file, MYF(MY_WME));
	msg("xtrabackup: Failed.\n");
	return(TRUE);
}

/***********************************************************************
Create an empty file with a given path and close it.
@return TRUE on succees, FALSE on error. */
static ibool
xb_create_suspend_file(const char *path)
{
	ibool			success;
	os_file_t		suspend_file = XB_FILE_UNDEFINED;

	/* xb_file_create reads srv_unix_file_flush_method */
	suspend_file = xb_file_create(path, OS_FILE_CREATE,
				      OS_FILE_NORMAL, OS_DATA_FILE,
				      &success);

	if (success && suspend_file != XB_FILE_UNDEFINED) {

		os_file_close(suspend_file);

		return(TRUE);
	}

	msg("xtrabackup: Error: failed to create file '%s'\n", path);

	return(FALSE);
}

/************************************************************************
Inittialize table filters for partial backup. */
static
void
xb_filters_init()
/*=============*/
{
	if (xtrabackup_tables) {
		/* init regexp */
		char *p, *next;
		int i;
		char errbuf[100];

		tables_regex_num = 1;

		p = xtrabackup_tables;
		while ((p = strchr(p, ',')) != NULL) {
			p++;
			tables_regex_num++;
		}

		tables_regex = static_cast<xb_regex_t *>
			(ut_malloc(sizeof(xb_regex_t) * tables_regex_num));

		p = xtrabackup_tables;
		for (i=0; i < tables_regex_num; i++) {
			next = strchr(p, ',');
			ut_a(next || i == tables_regex_num - 1);

			next++;
			if (i != tables_regex_num - 1)
				*(next - 1) = '\0';

			xb_regerror(xb_regcomp(&tables_regex[i], p,
					       REG_EXTENDED),
				    &tables_regex[i], errbuf, sizeof(errbuf));
			msg("xtrabackup: tables regcomp(%s): %s\n", p, errbuf);

			if (i != tables_regex_num - 1)
				*(next - 1) = ',';
			p = next;
		}
	}

	if (xtrabackup_tables_file) {
		char name_buf[NAME_LEN*2+2];
		FILE *fp;

		name_buf[NAME_LEN*2+1] = '\0';

		/* init tables_hash */
		tables_hash = hash_create(1000);

		/* read and store the filenames */
		fp = fopen(xtrabackup_tables_file,"r");
		if (!fp) {
			msg("xtrabackup: cannot open %s\n",
			    xtrabackup_tables_file);
			exit(EXIT_FAILURE);
		}
		for (;;) {
			xtrabackup_tables_t*	table;
			char*	p = name_buf;

			if ( fgets(name_buf, NAME_LEN*2+1, fp) == 0 ) {
				break;
			}

			p = strchr(name_buf, '\n');
			if (p)
			{
				*p = '\0';
			}

			table = static_cast<xtrabackup_tables_t *>
				(malloc(sizeof(xtrabackup_tables_t)
					+ strlen(name_buf) + 1));
			memset(table, '\0', sizeof(xtrabackup_tables_t) + strlen(name_buf) + 1);
			table->name = ((char*)table) + sizeof(xtrabackup_tables_t);
			strcpy(table->name, name_buf);

			HASH_INSERT(xtrabackup_tables_t, name_hash, tables_hash,
					ut_fold_string(table->name), table);

			msg("xtrabackup: table '%s' is registered to the "
			    "list.\n", table->name);
		}
	}
}


/************************************************************************
Destroy table filters for partial backup. */
static
void
xb_filters_free()
/*=============*/
{

	if (xtrabackup_tables_file) {
		ulint	i;

		/* free the hash elements */
		for (i = 0; i < hash_get_n_cells(tables_hash); i++) {
			xtrabackup_tables_t*	table;

			table = static_cast<xtrabackup_tables_t *>
				(HASH_GET_FIRST(tables_hash, i));

			while (table) {
				xtrabackup_tables_t*	prev_table = table;

				table = static_cast<xtrabackup_tables_t *>
					(HASH_GET_NEXT(name_hash, prev_table));

				HASH_DELETE(xtrabackup_tables_t, name_hash, tables_hash,
						ut_fold_string(prev_table->name), prev_table);
				free(prev_table);
			}
		}

		/* free tables_hash */
		hash_table_free(tables_hash);
	}

}

static void
xtrabackup_backup_func(void)
{
	MY_STAT stat_info;
	lsn_t latest_cp;
	char logfile_temp_path[FN_REFLEN];
	datasink_t		*ds;
	ds_ctxt_t		*ds_ctxt = NULL;
	ds_ctxt_t		*meta_ds_ctxt;
	char			suspend_path[FN_REFLEN];
	ibool			success;

#ifdef USE_POSIX_FADVISE
	msg("xtrabackup: uses posix_fadvise().\n");
#endif

	/* cd to datadir */

	if (my_setwd(mysql_real_data_home,MYF(MY_WME)))
	{
		msg("xtrabackup: cannot my_setwd %s\n", mysql_real_data_home);
		exit(EXIT_FAILURE);
	}
	msg("xtrabackup: cd to %s\n", mysql_real_data_home);

	mysql_data_home= mysql_data_home_buff;
	mysql_data_home[0]=FN_CURLIB;		// all paths are relative from here
	mysql_data_home[1]=0;

	/* set read only */
#if MYSQL_VERSION_ID >= 50600
	srv_read_only_mode = TRUE;
#else
	srv_read_only = TRUE;
#endif

	/* initialize components */
        if(innodb_init_param())
                exit(EXIT_FAILURE);

	xb_normalize_init_values();

#ifndef __WIN__        
        if (srv_file_flush_method_str == NULL) {
        	/* These are the default options */
#if (MYSQL_VERSION_ID < 50100)
		srv_unix_file_flush_method = SRV_UNIX_FDATASYNC;
#else /* MYSQL_VERSION_ID < 51000 */
		srv_unix_file_flush_method = SRV_UNIX_FSYNC;
#endif
#if (MYSQL_VERSION_ID < 50100)
	} else if (0 == ut_strcmp(srv_file_flush_method_str, "fdatasync")) {
	  	srv_unix_file_flush_method = SRV_UNIX_FDATASYNC;
#else /* MYSQL_VERSION_ID < 51000 */
	} else if (0 == ut_strcmp(srv_file_flush_method_str, "fsync")) {
		srv_unix_file_flush_method = SRV_UNIX_FSYNC;
#endif

	} else if (0 == ut_strcmp(srv_file_flush_method_str, "O_DSYNC")) {
	  	srv_unix_file_flush_method = SRV_UNIX_O_DSYNC;

	} else if (0 == ut_strcmp(srv_file_flush_method_str, "O_DIRECT")) {
	  	srv_unix_file_flush_method = SRV_UNIX_O_DIRECT;
		msg("xtrabackup: using O_DIRECT\n");
	} else if (0 == ut_strcmp(srv_file_flush_method_str, "O_DIRECT_NO_FSYNC")) {
		srv_unix_file_flush_method = SRV_UNIX_O_DIRECT_NO_FSYNC;
		msg("xtrabackup: using O_DIRECT_NO_FSYNC\n");
	} else if (0 == ut_strcmp(srv_file_flush_method_str, "littlesync")) {
	  	srv_unix_file_flush_method = SRV_UNIX_LITTLESYNC;

	} else if (0 == ut_strcmp(srv_file_flush_method_str, "nosync")) {
	  	srv_unix_file_flush_method = SRV_UNIX_NOSYNC;
#ifdef XTRADB_BASED
	} else if (0 == ut_strcmp(srv_file_flush_method_str, "ALL_O_DIRECT")) {
		srv_unix_file_flush_method = SRV_UNIX_ALL_O_DIRECT;
		msg("xtrabackup: using ALL_O_DIRECT\n");
#endif
#ifdef XTRADB_BASED
	} else if (0 == ut_strcmp(srv_file_flush_method_str, "ALL_O_DIRECT")) {
		srv_unix_file_flush_method = SRV_UNIX_ALL_O_DIRECT;
#endif
	} else {
	  	msg("xtrabackup: Unrecognized value %s for "
		    "innodb_flush_method\n", srv_file_flush_method_str);
	  	exit(EXIT_FAILURE);
	}
#else /* __WIN__ */
	/* We can only use synchronous unbuffered IO on Windows for now */
	if (srv_file_flush_method_str != NULL) {
		msg("xtrabackupp: Warning: "
		    "ignoring innodb_flush_method = %s on Windows.\n");
	}

	srv_win_file_flush_method = SRV_WIN_IO_UNBUFFERED;
	srv_use_native_aio = FALSE;
#endif

#ifndef INNODB_VERSION_SHORT
        if (srv_pool_size >= 1000 * 1024) {
#else
	if (srv_buf_pool_size >= 1000 * 1024 * 1024) {
#endif
                                  /* Here we still have srv_pool_size counted
                                  in kilobytes (in 4.0 this was in bytes)
				  srv_boot() converts the value to
                                  pages; if buffer pool is less than 1000 MB,
                                  assume fewer threads. */
                srv_max_n_threads = 50000;

#ifndef INNODB_VERSION_SHORT
        } else if (srv_pool_size >= 8 * 1024) {
#else
	} else if (srv_buf_pool_size >= 8 * 1024 * 1024) {
#endif

                srv_max_n_threads = 10000;
        } else {
		srv_max_n_threads = 1000;       /* saves several MB of memory,
                                                especially in 64-bit
                                                computers */
        }

	os_sync_mutex = NULL;
	srv_general_init();
#if MYSQL_VERSION_ID >= 50600
	ut_crc32_init();
#endif

	xb_filters_init();

	{
	ibool	log_file_created;
	ibool	log_created	= FALSE;
	ibool	log_opened	= FALSE;
	ulint	err;
	ulint	i;

	err = xb_data_files_init();
	if (err != DB_SUCCESS) {
		msg("xtrabackup: error: xb_data_files_init() failed with"
		    "error code %lu\n", err);
		exit(EXIT_FAILURE);
	}

	log_init();

	lock_sys_create(srv_lock_table_size);

	for (i = 0; i < srv_n_log_files; i++) {
		err = open_or_create_log_file(FALSE, &log_file_created,
							     log_opened, 0, i);
		if (err != DB_SUCCESS) {

			//return((int) err);
			exit(EXIT_FAILURE);
		}

		if (log_file_created) {
			log_created = TRUE;
		} else {
			log_opened = TRUE;
		}
		if ((log_opened && log_created)) {
			msg(
	"xtrabackup: Error: all log files must be created at the same time.\n"
	"xtrabackup: All log files must be created also in database creation.\n"
	"xtrabackup: If you want bigger or smaller log files, shut down the\n"
	"xtrabackup: database and make sure there were no errors in shutdown.\n"
	"xtrabackup: Then delete the existing log files. Edit the .cnf file\n"
	"xtrabackup: and start the database again.\n");

			//return(DB_ERROR);
			exit(EXIT_FAILURE);
		}
	}

	/* log_file_created must not be TRUE, if online */
	if (log_file_created) {
		msg("xtrabackup: Something wrong with source files...\n");
		exit(EXIT_FAILURE);
	}

	}

	/* create extra LSN dir if it does not exist. */
	if (xtrabackup_extra_lsndir
		&&!my_stat(xtrabackup_extra_lsndir,&stat_info,MYF(0))
		&& (my_mkdir(xtrabackup_extra_lsndir,0777,MYF(0)) < 0)){
		msg("xtrabackup: Error: cannot mkdir %d: %s\n",
		    my_errno, xtrabackup_extra_lsndir);
		exit(EXIT_FAILURE);
	}


	if (!xtrabackup_log_only) {

	/* create target dir if not exist */
	if (!my_stat(xtrabackup_target_dir,&stat_info,MYF(0))
		&& (my_mkdir(xtrabackup_target_dir,0777,MYF(0)) < 0)){
		msg("xtrabackup: Error: cannot mkdir %d: %s\n",
		    my_errno, xtrabackup_target_dir);
		exit(EXIT_FAILURE);
	}

	} else {
		msg("xtrabackup: Log only mode.\n");
	}

        {
        fil_system_t*   f_system = fil_system;

	/* definition from recv_recovery_from_checkpoint_start() */
	log_group_t*	max_cp_group;
	ulint		max_cp_field;
	byte*		buf;
	byte*		log_hdr_buf_;
	byte*		log_hdr_buf;
	ulint		err;

	/* start back ground thread to copy newer log */
	os_thread_id_t log_copying_thread_id;
	datafiles_iter_t *it;

	log_hdr_buf_ = static_cast<byte *>
		(ut_malloc(LOG_FILE_HDR_SIZE + OS_FILE_LOG_BLOCK_SIZE));
	log_hdr_buf = static_cast<byte *>
		(ut_align(log_hdr_buf_, OS_FILE_LOG_BLOCK_SIZE));

	/* get current checkpoint_lsn */
	/* Look for the latest checkpoint from any of the log groups */

	mutex_enter(&log_sys->mutex);

	err = recv_find_max_checkpoint(&max_cp_group, &max_cp_field);

	if (err != DB_SUCCESS) {

		ut_free(log_hdr_buf_);
		exit(EXIT_FAILURE);
	}
		
	log_group_read_checkpoint_info(max_cp_group, max_cp_field);
	buf = log_sys->checkpoint_buf;

	checkpoint_lsn_start = MACH_READ_64(buf + LOG_CHECKPOINT_LSN);
	checkpoint_no_start = MACH_READ_64(buf + LOG_CHECKPOINT_NO);

	mutex_exit(&log_sys->mutex);

reread_log_header:
#ifdef INNODB_VERSION_SHORT
	fil_io(OS_FILE_READ | OS_FILE_LOG, TRUE, max_cp_group->space_id,
			0, 0, 0, LOG_FILE_HDR_SIZE, log_hdr_buf, max_cp_group);
#else
	fil_io(OS_FILE_READ | OS_FILE_LOG, TRUE, max_cp_group->space_id,
			0, 0, LOG_FILE_HDR_SIZE, log_hdr_buf, max_cp_group);
#endif

	/* check consistency of log file header to copy */
	mutex_enter(&log_sys->mutex);

	err = recv_find_max_checkpoint(&max_cp_group, &max_cp_field);

        if (err != DB_SUCCESS) {

		ut_free(log_hdr_buf_);
                exit(EXIT_FAILURE);
        }

        log_group_read_checkpoint_info(max_cp_group, max_cp_field);
        buf = log_sys->checkpoint_buf;

	if(ut_dulint_cmp(checkpoint_no_start,
			MACH_READ_64(buf + LOG_CHECKPOINT_NO)) != 0) {
		checkpoint_lsn_start = MACH_READ_64(buf + LOG_CHECKPOINT_LSN);
		checkpoint_no_start = MACH_READ_64(buf + LOG_CHECKPOINT_NO);
		mutex_exit(&log_sys->mutex);
		goto reread_log_header;
	}

	mutex_exit(&log_sys->mutex);

	/* open the log file */
	if (!xtrabackup_log_only) {
		/* The xbstream format allows concurrent files streaming */
		if (xtrabackup_stream) {
			dst_log_fd = xtrabackup_create_tmpfile(
				logfile_temp_path, XB_LOG_FILENAME);
			if (dst_log_fd < 0) {
				msg("xtrabackup: error: "
				    "xtrabackup_create_tmpfile() failed. "
				    "(errno: %d)\n", my_errno);
				ut_free(log_hdr_buf_);
				exit(EXIT_FAILURE);
			}
		} else {
			fn_format(dst_log_path, XB_LOG_FILENAME,
				  xtrabackup_target_dir, "", MYF(0));
			dst_log_fd = my_create(dst_log_path, 0,
					       O_RDWR | O_BINARY | O_EXCL |
					       O_NOFOLLOW, MYF(MY_WME));
			if (dst_log_fd < 0) {
				msg("xtrabackup: error: cannot open %s "
				    "(errno: %d)\n", dst_log_path, my_errno);
				ut_free(log_hdr_buf_);
				exit(EXIT_FAILURE);
			}
		}
	} else {
		dst_log_fd = dup(fileno(stdout));
		if (dst_log_fd < 0) {
			msg("xtrabackup: error: dup() failed (errno: %d)",
			    errno);
			ut_free(log_hdr_buf_);
			exit(EXIT_FAILURE);
		}
	}

	/* label it */
	strcpy((char*) log_hdr_buf + LOG_FILE_WAS_CREATED_BY_HOT_BACKUP,
		"xtrabkup ");
	ut_sprintf_timestamp(
		(char*) log_hdr_buf + (LOG_FILE_WAS_CREATED_BY_HOT_BACKUP
				+ (sizeof "xtrabkup ") - 1));

	if (my_write(dst_log_fd, log_hdr_buf, LOG_FILE_HDR_SIZE,
		     MYF(MY_WME | MY_NABP))) {
		ut_free(log_hdr_buf_);
		exit(EXIT_FAILURE);
	}

	ut_free(log_hdr_buf_);

	/* start flag */
	log_copying = TRUE;

	/* start io throttle */
	if(xtrabackup_throttle) {
		os_thread_id_t io_watching_thread_id;

		io_ticket = xtrabackup_throttle;
		wait_throttle = xb_os_event_create(NULL);

		os_thread_create(io_watching_thread, NULL, &io_watching_thread_id);
	}


	/* copy log file by current position */
	if(xtrabackup_copy_logfile(checkpoint_lsn_start, FALSE))
		exit(EXIT_FAILURE);


	os_thread_create(log_copying_thread, NULL, &log_copying_thread_id);

	if (xtrabackup_parallel > 1 && xtrabackup_stream &&
	    xtrabackup_stream_fmt == XB_STREAM_FMT_TAR) {
		msg("xtrabackup: warning: the --parallel option does not have "
		    "any effect when streaming in the 'tar' format. "
		    "You can use the 'xbstream' format instead.\n");
		xtrabackup_parallel = 1;
	}

	/* Initialize the appropriate datasink */
	if (xtrabackup_log_only) {
		ds = &datasink_local;
	} else if (xtrabackup_compress) {
		ds = &datasink_compress;
	} else if (xtrabackup_stream) {
		ds = &datasink_stream;
	} else {
		ds = &datasink_local;
	}

	ds_ctxt = ds->init(xtrabackup_target_dir);
	if (ds_ctxt == NULL) {
		msg("xtrabackup: Error: failed to initialize the "
		    "datasink.\n");
		exit(EXIT_FAILURE);
	}

	if (!xtrabackup_log_only) {
		uint			i;
		uint			count;
		os_ib_mutex_t		count_mutex;
		data_thread_ctxt_t 	*data_threads;

		ut_a(xtrabackup_parallel > 0);

		if (xtrabackup_parallel > 1) {
			msg("xtrabackup: Starting %u threads for parallel data "
			    "files transfer\n", xtrabackup_parallel);
		}

		it = datafiles_iter_new(f_system);
		if (it == NULL) {
			msg("xtrabackup: Error: "
			    "datafiles_iter_new() failed.\n");
			exit(EXIT_FAILURE);
		}

		/* Create data copying threads */
		data_threads = (data_thread_ctxt_t *)
			ut_malloc(sizeof(data_thread_ctxt_t) *
				  xtrabackup_parallel);
		count = xtrabackup_parallel;
		count_mutex = OS_MUTEX_CREATE();

		for (i = 0; i < (uint) xtrabackup_parallel; i++) {
			data_threads[i].it = it;
			data_threads[i].num = i+1;
			data_threads[i].count = &count;
			data_threads[i].count_mutex = count_mutex;
			data_threads[i].ds_ctxt = ds_ctxt;
			os_thread_create(data_copy_thread_func,
					 data_threads + i,
					 &data_threads[i].id);
		}

		/* Wait for threads to exit */
		while (1) {
			os_thread_sleep(1000000);
			os_mutex_enter(count_mutex);
			if (count == 0) {
				os_mutex_exit(count_mutex);
				break;
			}
			os_mutex_exit(count_mutex);
		}

		os_mutex_free(count_mutex);
		ut_free(data_threads);
		datafiles_iter_free(it);

	}

        }

	/* Close the datasync to flush any buffered data it might have,
	so we don't end up with buffered data being written after the stream
	produced by innobackupex. */
	ds->deinit(ds_ctxt);

	/* suspend-at-end */
	if (xtrabackup_suspend_at_end) {
		ibool		exists;
		os_file_type_t	type;

		sprintf(suspend_path, "%s%s", xtrabackup_target_dir,
			"/xtrabackup_suspended");
		srv_normalize_path_for_win(suspend_path);

		if (!xb_create_suspend_file(suspend_path)) {
			exit(EXIT_FAILURE);
		}

		exists = TRUE;
		while (exists) {
			os_thread_sleep(200000); /*0.2 sec*/
			success = os_file_status(suspend_path, &exists, &type);
			/* success == FALSE if file exists, but stat() failed.
			os_file_status() prints an error message in this case */
			ut_a(success);
		}
	}

	/* Reinit the datasink */
	ds_ctxt = ds->init(xtrabackup_target_dir);

	/* read the latest checkpoint lsn */
	latest_cp = ut_dulint_zero;
	{
		log_group_t*	max_cp_group;
		ulint	max_cp_field;
		ulint	err;

		mutex_enter(&log_sys->mutex);

		err = recv_find_max_checkpoint(&max_cp_group, &max_cp_field);

		if (err != DB_SUCCESS) {
			msg("xtrabackup: Error: recv_find_max_checkpoint() failed.\n");
			mutex_exit(&log_sys->mutex);
			goto skip_last_cp;
		}

		log_group_read_checkpoint_info(max_cp_group, max_cp_field);

		latest_cp = MACH_READ_64(log_sys->checkpoint_buf + LOG_CHECKPOINT_LSN);

		mutex_exit(&log_sys->mutex);

#ifndef INNODB_VERSION_SHORT
		msg("xtrabackup: The latest check point (for incremental): "
		    "'%lu:%lu'\n", latest_cp.high, latest_cp.low);
#else
		msg("xtrabackup: The latest check point (for incremental): "
		    "'" LSN_PF "'\n", latest_cp);
#endif
	}
skip_last_cp:
	/* stop log_copying_thread */
	log_copying = FALSE;
	msg("xtrabackup: Stopping log copying thread.\n");
	while (log_copying_running) {
		msg(".");
		os_thread_sleep(200000); /*0.2 sec*/
	}
	msg("\n");

	if (!log_copying_succeed) {
		msg("xtrabackup: Error: log_copying_thread failed.\n");
		exit(EXIT_FAILURE);
	}

	/* Signal innobackupex that log copying has stopped and it may now
	unlock tables, so we can possibly stream xtrabackup_logfile later
	without holding the lock. */
	if (xtrabackup_suspend_at_end &&
	    !xb_create_suspend_file(suspend_path)) {
		exit(EXIT_FAILURE);
	}

	if(!xtrabackup_incremental) {
		strcpy(metadata_type, "full-backuped");
		metadata_from_lsn = ut_dulint_zero;
	} else {
		strcpy(metadata_type, "incremental");
		metadata_from_lsn = incremental_lsn;
	}
	metadata_to_lsn = latest_cp;
	metadata_last_lsn = log_copy_scanned_lsn;

	/* Write xtrabackup_checkpoint without compression, if it was used for
	backup. */
	meta_ds_ctxt = xtrabackup_compress ?
		compress_get_dest_ctxt(ds_ctxt) : ds_ctxt;
	if (!xtrabackup_stream_metadata(meta_ds_ctxt))
		msg("xtrabackup: error:"
		    "xtrabackup_stream_metadata() failed.\n");

	if (xtrabackup_extra_lsndir) {
		char	filename[FN_REFLEN];

		sprintf(filename, "%s/%s", xtrabackup_extra_lsndir,
			XTRABACKUP_METADATA_FILENAME);
		if (!xtrabackup_write_metadata(filename))
			msg("xtrabackup: error: "
			    "xtrabackup_write_metadata() failed.\n");

	}

	/* Stream the transaction log from the temporary file */
	if (!xtrabackup_log_only && xtrabackup_stream &&
	    xtrabackup_stream_temp_logfile(dst_log_fd, ds_ctxt)) {
		msg("xtrabackup: Error: failed to stream the log "
		    "from the temporary file %s", logfile_temp_path);
		exit(EXIT_FAILURE);
	}

	ds->deinit(ds_ctxt);

	if (wait_throttle)
		os_event_free(wait_throttle);

#ifndef INNODB_VERSION_SHORT
	msg("xtrabackup: Transaction log of lsn (%lu %lu) to (%lu %lu) was "
	    "copied.\n", checkpoint_lsn_start.high, checkpoint_lsn_start.low,
	    log_copy_scanned_lsn.high, log_copy_scanned_lsn.low);
#else
	msg("xtrabackup: Transaction log of lsn (" LSN_PF ") to (" LSN_PF
	    ") was copied.\n", checkpoint_lsn_start, log_copy_scanned_lsn);
#endif

	xb_data_files_close();

	xb_filters_free();
}

/* ================= stats ================= */
static my_bool
xtrabackup_stats_level(
	dict_index_t*	index,
	ulint		level)
{
	ulint	space;
	page_t*	page;

	rec_t*	node_ptr;

	ulint	right_page_no;

	page_cur_t	cursor;

	mtr_t	mtr;
	mem_heap_t*	heap	= mem_heap_create(256);

	ulint*	offsets = NULL;

	ulonglong n_pages, n_pages_extern;
	ulonglong sum_data, sum_data_extern;
	ulonglong n_recs;
	ulint	page_size;
#ifdef INNODB_VERSION_SHORT
	buf_block_t*	block;
	ulint	zip_size;
#endif

	n_pages = sum_data = n_recs = 0;
	n_pages_extern = sum_data_extern = 0;


	if (level == 0)
		fprintf(stdout, "        leaf pages: ");
	else
		fprintf(stdout, "     level %lu pages: ", level);

	mtr_start(&mtr);

#ifndef INNODB_VERSION_SHORT
#if (MYSQL_VERSION_ID < 50100)
	mtr_x_lock(&(index->tree->lock), &mtr);
	page = btr_root_get(index->tree, &mtr);
#else /* MYSQL_VERSION_ID < 51000 */
	mtr_x_lock(&(index->lock), &mtr);
	page = btr_root_get(index, &mtr);
#endif
#else
	mtr_x_lock(&(index->lock), &mtr);
	block = xb_btr_root_block_get(index, RW_X_LATCH, &mtr);
	page = buf_block_get_frame(block);
#endif

#ifndef INNODB_VERSION_SHORT
	space = buf_frame_get_space_id(page);
#else
	space = page_get_space_id(page);
	zip_size = fil_space_get_zip_size(space);
#endif

	while (level != btr_page_get_level(page, &mtr)) {

#ifndef INNODB_VERSION_SHORT
		ut_a(btr_page_get_level(page, &mtr) > 0);
#else
		ut_a(space == buf_block_get_space(block));
		ut_a(space == page_get_space_id(page));
		ut_a(!page_is_leaf(page));
#endif

#ifndef INNODB_VERSION_SHORT
		page_cur_set_before_first(page, &cursor);
#else
		page_cur_set_before_first(block, &cursor);
#endif
		page_cur_move_to_next(&cursor);

		node_ptr = page_cur_get_rec(&cursor);
		offsets = rec_get_offsets(node_ptr, index, offsets,
					ULINT_UNDEFINED, &heap);
#ifndef INNODB_VERSION_SHORT
		page = btr_node_ptr_get_child(node_ptr, offsets, &mtr);
#else
		block = btr_node_ptr_get_child(node_ptr, index, offsets, &mtr);
		page = buf_block_get_frame(block);
#endif
	}

loop:
	mem_heap_empty(heap);
	offsets = NULL;
#if (MYSQL_VERSION_ID < 50100)
	mtr_x_lock(&(index->tree->lock), &mtr);
#else /* MYSQL_VERSION_ID < 51000 */
	mtr_x_lock(&(index->lock), &mtr);
#endif

	right_page_no = btr_page_get_next(page, &mtr);


	/*=================================*/
	//fprintf(stdout, "%lu ", (ulint) buf_frame_get_page_no(page));

	n_pages++;
	sum_data += page_get_data_size(page);
	n_recs += page_get_n_recs(page);


	if (level == 0) {
		page_cur_t	cur;
		ulint	n_fields;
		ulint	i;
		mem_heap_t*	local_heap	= NULL;
		ulint	offsets_[REC_OFFS_NORMAL_SIZE];
		ulint*	local_offsets	= offsets_;

		*offsets_ = (sizeof offsets_) / sizeof *offsets_;

#ifndef INNODB_VERSION_SHORT
		page_cur_set_before_first(page, &cur);
#else
		page_cur_set_before_first(block, &cur);
#endif
		page_cur_move_to_next(&cur);

		for (;;) {
			if (page_cur_is_after_last(&cur)) {
				break;
			}

			local_offsets = rec_get_offsets(cur.rec, index, local_offsets,
						ULINT_UNDEFINED, &local_heap);
			n_fields = rec_offs_n_fields(local_offsets);

			for (i = 0; i < n_fields; i++) {
				if (rec_offs_nth_extern(local_offsets, i)) {
					page_t*	local_page;
					ulint	space_id;
					ulint	page_no;
					ulint	offset;
					byte*	blob_header;
					ulint	part_len;
					mtr_t	local_mtr;
					ulint	local_len;
					byte*	data;
#ifdef INNODB_VERSION_SHORT
					buf_block_t*	local_block;
#endif

					data = rec_get_nth_field(cur.rec, local_offsets, i, &local_len);

					ut_a(local_len >= BTR_EXTERN_FIELD_REF_SIZE);
					local_len -= BTR_EXTERN_FIELD_REF_SIZE;

					space_id = mach_read_from_4(data + local_len + BTR_EXTERN_SPACE_ID);
					page_no = mach_read_from_4(data + local_len + BTR_EXTERN_PAGE_NO);
					offset = mach_read_from_4(data + local_len + BTR_EXTERN_OFFSET);

					if (offset != FIL_PAGE_DATA)
						msg("\nWarning: several record may share same external page.\n");

					for (;;) {
						mtr_start(&local_mtr);

#ifndef INNODB_VERSION_SHORT
						local_page = buf_page_get(space_id, page_no, RW_S_LATCH, &local_mtr);
#ifdef UNIV_SYNC_DEBUG
						buf_page_dbg_add_level(local_page, SYNC_NO_ORDER_CHECK);
#endif
#else
#if (MYSQL_VERSION_ID < 50517)
						local_block = btr_block_get(space_id, zip_size, page_no, RW_S_LATCH, &local_mtr);
#else
						local_block = btr_block_get(space_id, zip_size, page_no, RW_S_LATCH, index, &local_mtr);
#endif
						local_page = buf_block_get_frame(local_block);
#endif
						blob_header = local_page + offset;
#define BTR_BLOB_HDR_PART_LEN		0
#define BTR_BLOB_HDR_NEXT_PAGE_NO	4
						//part_len = btr_blob_get_part_len(blob_header);
						part_len = mach_read_from_4(blob_header + BTR_BLOB_HDR_PART_LEN);

						//page_no = btr_blob_get_next_page_no(blob_header);
						page_no = mach_read_from_4(blob_header + BTR_BLOB_HDR_NEXT_PAGE_NO);

						offset = FIL_PAGE_DATA;




						/*=================================*/
						//fprintf(stdout, "[%lu] ", (ulint) buf_frame_get_page_no(page));

						n_pages_extern++;
						sum_data_extern += part_len;


						mtr_commit(&local_mtr);

						if (page_no == FIL_NULL)
							break;
					}
				}
			}

			page_cur_move_to_next(&cur);
		}
	}




	mtr_commit(&mtr);
	if (right_page_no != FIL_NULL) {
		mtr_start(&mtr);
#ifndef INNODB_VERSION_SHORT
		page = btr_page_get(space, right_page_no, RW_X_LATCH, &mtr);
#else
#if (MYSQL_VERSION_ID < 50517)
		block = btr_block_get(space, zip_size, right_page_no,
				      RW_X_LATCH, &mtr);
#else
		block = btr_block_get(space, zip_size, right_page_no,
				      RW_X_LATCH, index, &mtr);
#endif
		page = buf_block_get_frame(block);
#endif
		goto loop;
	}
	mem_heap_free(heap);

#ifndef INNODB_VERSION_SHORT
	page_size = UNIV_PAGE_SIZE;
#else
	if (zip_size) {
		page_size = zip_size;
	} else {
		page_size = UNIV_PAGE_SIZE;
	}
#endif

	if (level == 0)
		fprintf(stdout, "recs=%llu, ", n_recs);

	fprintf(stdout, "pages=%llu, data=%llu bytes, data/pages=%lld%%",
		n_pages, sum_data,
		((sum_data * 100)/ page_size)/n_pages);


	if (level == 0 && n_pages_extern) {
		putc('\n', stdout);
		/* also scan blob pages*/
		fprintf(stdout, "    external pages: ");

		fprintf(stdout, "pages=%llu, data=%llu bytes, data/pages=%lld%%",
			n_pages_extern, sum_data_extern,
			((sum_data_extern * 100)/ page_size)/n_pages_extern);
	}

	putc('\n', stdout);

	if (level > 0) {
		xtrabackup_stats_level(index, level - 1);
	}

	return(TRUE);
}

static void
xtrabackup_stats_func(void)
{
	ulint n;

	/* cd to datadir */

	if (my_setwd(mysql_real_data_home,MYF(MY_WME)))
	{
		msg("xtrabackup: cannot my_setwd %s\n", mysql_real_data_home);
		exit(EXIT_FAILURE);
	}
	msg("xtrabackup: cd to %s\n", mysql_real_data_home);

	mysql_data_home= mysql_data_home_buff;
	mysql_data_home[0]=FN_CURLIB;		// all paths are relative from here
	mysql_data_home[1]=0;

	/* set read only */
#if MYSQL_VERSION_ID >= 50600
	srv_read_only_mode = TRUE;
#else
	srv_read_only = TRUE;
	srv_fake_write = TRUE;
#endif
#if (MYSQL_VERSION_ID >= 50500) && (MYSQL_VERSION_ID < 50600)
	/* AIO is incompatible with srv_read_only/srv_fake_write */
	srv_use_native_aio = FALSE;
#endif

	/* initialize components */
	if(innodb_init_param())
		exit(EXIT_FAILURE);

	/* Check if the log files have been created, otherwise innodb_init()
	will crash when called with srv_read_only == TRUE */
	for (n = 0; n < srv_n_log_files; n++) {
		char		logname[FN_REFLEN];
		ibool		exists;
		os_file_type_t	type;

		sprintf(logname, "ib_logfile%lu", (ulong) n);
		if (!os_file_status(logname, &exists, &type) || !exists ||
		    type != OS_FILE_TYPE_FILE) {
			msg("xtrabackup: Error: "
			    "Cannot find log file %s.\n", logname);
			msg("xtrabackup: Error: "
			    "to use the statistics feature, you need a "
			    "clean copy of the database including "
			    "correctly sized log files, so you need to "
			    "execute with --prepare twice to use this "
			    "functionality on a backup.\n");
			exit(EXIT_FAILURE);
		}
	}

	msg("xtrabackup: Starting 'read-only' InnoDB instance to gather "
	    "index statistics.\n"
	    "xtrabackup: Using %lld bytes for buffer pool (set by "
	    "--use-memory parameter)\n", xtrabackup_use_memory);

	if(innodb_init())
		exit(EXIT_FAILURE);

	xb_filters_init();

	fprintf(stdout, "\n\n<INDEX STATISTICS>\n");

	/* gather stats */

	{
	dict_table_t*	sys_tables;
	dict_index_t*	sys_index;
	dict_table_t*	table;
	btr_pcur_t	pcur;
	rec_t*		rec;
	byte*		field;
	ulint		len;
	mtr_t		mtr;
	
	/* Enlarge the fatal semaphore wait timeout during the InnoDB table
	monitor printout */

#if MYSQL_VERSION_ID >= 50600
	os_increment_counter_by_amount(server_mutex,
				       srv_fatal_semaphore_wait_threshold,
				       72000);
#else
	mutex_enter(&kernel_mutex);
	srv_fatal_semaphore_wait_threshold += 72000; /* 20 hours */
	mutex_exit(&kernel_mutex);
#endif

	mutex_enter(&(dict_sys->mutex));

	mtr_start(&mtr);

	sys_tables = dict_table_get_low("SYS_TABLES");
	sys_index = UT_LIST_GET_FIRST(sys_tables->indexes);

	xb_btr_pcur_open_at_index_side(TRUE, sys_index, BTR_SEARCH_LEAF, &pcur,
				       TRUE, 0, &mtr);
loop:
	btr_pcur_move_to_next_user_rec(&pcur, &mtr);

	rec = btr_pcur_get_rec(&pcur);

#ifndef INNODB_VERSION_SHORT
	if (!btr_pcur_is_on_user_rec(&pcur, &mtr))
#else
	if (!btr_pcur_is_on_user_rec(&pcur))
#endif
	{
		/* end of index */

		btr_pcur_close(&pcur);
		mtr_commit(&mtr);
		
		mutex_exit(&(dict_sys->mutex));

		/* Restore the fatal semaphore wait timeout */
#if MYSQL_VERSION_ID >= 50600
		os_increment_counter_by_amount(server_mutex,
					       srv_fatal_semaphore_wait_threshold,
					       -72000);
#else
		mutex_enter(&kernel_mutex);
		srv_fatal_semaphore_wait_threshold -= 72000; /* 20 hours */
		mutex_exit(&kernel_mutex);
#endif

		goto end;
	}	

	field = rec_get_nth_field_old(rec, 0, &len);

#if (MYSQL_VERSION_ID < 50100)
	if (!rec_get_deleted_flag(rec, sys_tables->comp))
#else /* MYSQL_VERSION_ID < 51000 */
	if (!rec_get_deleted_flag(rec, 0))
#endif
	{

		/* We found one */

                char*	table_name = mem_strdupl((char*) field, len);

		btr_pcur_store_position(&pcur, &mtr);

		mtr_commit(&mtr);

		table = dict_table_get_low(table_name);
		mem_free(table_name);

		if (table && check_if_skip_table(table->name, ""))
			goto skip;

		if (table == NULL) {
			fputs("InnoDB: Failed to load table ", stderr);
#if (MYSQL_VERSION_ID < 50100)
			ut_print_namel(stderr, NULL, (char*) field, len);
#else /* MYSQL_VERSION_ID < 51000 */
			ut_print_namel(stderr, NULL, TRUE, (char*) field, len);
#endif
			putc('\n', stderr);
		} else {
			dict_index_t*	index;

			/* The table definition was corrupt if there
			is no index */

			if (dict_table_get_first_index(table)) {
				dict_stats_update_transient(table);
			}

			//dict_table_print_low(table);

			index = UT_LIST_GET_FIRST(table->indexes);
			while (index != NULL) {
{
	IB_INT64	n_vals;

	if (index->n_user_defined_cols > 0) {
		n_vals = index->stat_n_diff_key_vals[
					index->n_user_defined_cols];
	} else {
		n_vals = index->stat_n_diff_key_vals[1];
	}

	fprintf(stdout,
		"  table: %s, index: %s, space id: %lu, root page: %lu"
#ifdef INNODB_VERSION_SHORT
		", zip size: %lu"
#endif
		"\n  estimated statistics in dictionary:\n"
		"    key vals: %lu, leaf pages: %lu, size pages: %lu\n"
		"  real statistics:\n",
		table->name, index->name,
		(ulong) index->space,
#if (MYSQL_VERSION_ID < 50100)
		(ulong) index->tree->page,
#else /* MYSQL_VERSION_ID < 51000 */
		(ulong) index->page,
#endif
#ifdef INNODB_VERSION_SHORT
		(ulong) fil_space_get_zip_size(index->space),
#endif
		(ulong) n_vals,
		(ulong) index->stat_n_leaf_pages,
		(ulong) index->stat_index_size);

	{
		mtr_t	local_mtr;
		page_t*	root;
		ulint	page_level;

		mtr_start(&local_mtr);

#if (MYSQL_VERSION_ID < 50100)
		mtr_x_lock(&(index->tree->lock), &local_mtr);
		root = btr_root_get(index->tree, &local_mtr);
#else /* MYSQL_VERSION_ID < 51000 */
		mtr_x_lock(&(index->lock), &local_mtr);
		root = btr_root_get(index, &local_mtr);
#endif
		page_level = btr_page_get_level(root, &local_mtr);

		xtrabackup_stats_level(index, page_level);

		mtr_commit(&local_mtr);
	}

	putc('\n', stdout);
}
				index = UT_LIST_GET_NEXT(indexes, index);
			}
		}

skip:
		mtr_start(&mtr);

		btr_pcur_restore_position(BTR_SEARCH_LEAF, &pcur, &mtr);
	}

	goto loop;
	}

end:
	putc('\n', stdout);

	xb_filters_free();

	/* shutdown InnoDB */
	if(innodb_end())
		exit(EXIT_FAILURE);
}

/* ================= prepare ================= */

/*****************************************************************//**
Wrapper around around recv_check_cp_is_consistent() for handling
version differences.

@return TRUE if checkpoint info in the buffer is OK */
static ibool
xb_recv_check_cp_is_consistent(
/*===========================*/
	const byte*	buf)	/*!<in: buffer containing checkpoint info */
{
#if MYSQL_VERSION_ID >= 50600
	return recv_check_cp_is_consistent(buf);
#else
	return recv_check_cp_is_consistent(const_cast<byte *>(buf));
#endif
}

static my_bool
xtrabackup_init_temp_log(void)
{
	os_file_t	src_file = XB_FILE_UNDEFINED;
	char	src_path[FN_REFLEN];
	char	dst_path[FN_REFLEN];
	ibool	success;

	ulint	field;
	byte*	log_buf;
	byte*	log_buf_ = NULL;

	IB_INT64	file_size;

	lsn_t	max_no;
	lsn_t	max_lsn;
	lsn_t	checkpoint_no;

	ulint	fold;

	max_no = ut_dulint_zero;

	if (!xb_init_log_block_size()) {
		goto error;
	}

	if(!xtrabackup_incremental_dir) {
		sprintf(dst_path, "%s/ib_logfile0", xtrabackup_target_dir);
		sprintf(src_path, "%s/%s", xtrabackup_target_dir,
			XB_LOG_FILENAME);
	} else {
		sprintf(dst_path, "%s/ib_logfile0", xtrabackup_incremental_dir);
		sprintf(src_path, "%s/%s", xtrabackup_incremental_dir,
			XB_LOG_FILENAME);
	}

	srv_normalize_path_for_win(dst_path);
	srv_normalize_path_for_win(src_path);
retry:
	src_file = xb_file_create_no_error_handling(src_path, OS_FILE_OPEN,
						    OS_FILE_READ_WRITE,
						    &success);
	if (!success) {
		/* The following call prints an error message */
		os_file_get_last_error(TRUE);

		msg("xtrabackup: Warning: cannot open %s. will try to find.\n",
		    src_path);

		/* check if ib_logfile0 may be xtrabackup_logfile */
		src_file = xb_file_create_no_error_handling(dst_path,
							    OS_FILE_OPEN,
							    OS_FILE_READ_WRITE,
							    &success);
		if (!success) {
			os_file_get_last_error(TRUE);
			msg("  xtrabackup: Fatal error: cannot find %s.\n",
			    src_path);

			goto error;
		}

		log_buf_ = static_cast<byte *>
			(ut_malloc(LOG_FILE_HDR_SIZE * 2));
		log_buf = static_cast<byte *>
			(ut_align(log_buf_, LOG_FILE_HDR_SIZE));

		success = xb_os_file_read(src_file, log_buf, 0,
					  LOG_FILE_HDR_SIZE);
		if (!success) {
			goto error;
		}

		if ( ut_memcmp(log_buf + LOG_FILE_WAS_CREATED_BY_HOT_BACKUP,
				(byte*)"xtrabkup", (sizeof "xtrabkup") - 1) == 0) {
			msg("  xtrabackup: 'ib_logfile0' seems to be "
			    "'xtrabackup_logfile'. will retry.\n");

			ut_free(log_buf_);
			log_buf_ = NULL;

			os_file_close(src_file);
			src_file = XB_FILE_UNDEFINED;

			/* rename and try again */
			success = xb_file_rename(dst_path, src_path);
			if (!success) {
				goto error;
			}

			goto retry;
		}

		msg("  xtrabackup: Fatal error: cannot find %s.\n",
		src_path);

		ut_free(log_buf_);
		log_buf_ = NULL;

		os_file_close(src_file);
		src_file = XB_FILE_UNDEFINED;

		goto error;
	}

#ifdef USE_POSIX_FADVISE
	posix_fadvise(src_file, 0, 0, POSIX_FADV_SEQUENTIAL);
	posix_fadvise(src_file, 0, 0, POSIX_FADV_DONTNEED);
#endif

	xb_file_set_nocache(src_file, src_path, "OPEN");

	file_size = os_file_get_size(src_file);


	/* TODO: We should skip the following modifies, if it is not the first time. */
	log_buf_ = static_cast<byte *>(ut_malloc(UNIV_PAGE_SIZE * 129));
	log_buf = static_cast<byte *>(ut_align(log_buf_, UNIV_PAGE_SIZE));

	/* read log file header */
	success = xb_os_file_read(src_file, log_buf, 0, LOG_FILE_HDR_SIZE);
	if (!success) {
		goto error;
	}

	if ( ut_memcmp(log_buf + LOG_FILE_WAS_CREATED_BY_HOT_BACKUP,
			(byte*)"xtrabkup", (sizeof "xtrabkup") - 1) != 0 ) {
		msg("xtrabackup: notice: xtrabackup_logfile was already used "
		    "to '--prepare'.\n");
		goto skip_modify;
	} else {
		/* clear it later */
		//memset(log_buf + LOG_FILE_WAS_CREATED_BY_HOT_BACKUP,
		//		' ', 4);
	}

	/* read last checkpoint lsn */
	for (field = LOG_CHECKPOINT_1; field <= LOG_CHECKPOINT_2;
			field += LOG_CHECKPOINT_2 - LOG_CHECKPOINT_1) {
		if (!xb_recv_check_cp_is_consistent(const_cast<const byte *>
						    (log_buf + field)))
			goto not_consistent;

		checkpoint_no = MACH_READ_64(log_buf + field + LOG_CHECKPOINT_NO);

		if (ut_dulint_cmp(checkpoint_no, max_no) >= 0) {
			max_no = checkpoint_no;
			max_lsn = MACH_READ_64(log_buf + field + LOG_CHECKPOINT_LSN);
/*
			mach_write_to_4(log_buf + field + LOG_CHECKPOINT_OFFSET,
					LOG_FILE_HDR_SIZE + ut_dulint_minus(max_lsn,
					ut_dulint_align_down(max_lsn,OS_FILE_LOG_BLOCK_SIZE)));

			ulint	fold;
			fold = ut_fold_binary(log_buf + field, LOG_CHECKPOINT_CHECKSUM_1);
			mach_write_to_4(log_buf + field + LOG_CHECKPOINT_CHECKSUM_1, fold);

			fold = ut_fold_binary(log_buf + field + LOG_CHECKPOINT_LSN,
				LOG_CHECKPOINT_CHECKSUM_2 - LOG_CHECKPOINT_LSN);
			mach_write_to_4(log_buf + field + LOG_CHECKPOINT_CHECKSUM_2, fold);
*/
		}
not_consistent:
		;
	}

	if (ut_dulint_cmp(max_no, ut_dulint_zero) == 0) {
		msg("xtrabackup: No valid checkpoint found.\n");
		goto error;
	}


	/* It seems to be needed to overwrite the both checkpoint area. */
	MACH_WRITE_64(log_buf + LOG_CHECKPOINT_1 + LOG_CHECKPOINT_LSN, max_lsn);
	mach_write_to_4(log_buf + LOG_CHECKPOINT_1
			+ LOG_CHECKPOINT_OFFSET_LOW32,
			LOG_FILE_HDR_SIZE + (ulint) ut_dulint_minus(max_lsn,
			ut_dulint_align_down(max_lsn,OS_FILE_LOG_BLOCK_SIZE)));
#ifdef XTRADB_BASED
	MACH_WRITE_64(log_buf + LOG_CHECKPOINT_1 + LOG_CHECKPOINT_ARCHIVED_LSN,
			(ib_uint64_t)(LOG_FILE_HDR_SIZE + ut_dulint_minus(max_lsn,
					ut_dulint_align_down(max_lsn,OS_FILE_LOG_BLOCK_SIZE))));
#endif
	fold = ut_fold_binary(log_buf + LOG_CHECKPOINT_1, LOG_CHECKPOINT_CHECKSUM_1);
	mach_write_to_4(log_buf + LOG_CHECKPOINT_1 + LOG_CHECKPOINT_CHECKSUM_1, fold);

	fold = ut_fold_binary(log_buf + LOG_CHECKPOINT_1 + LOG_CHECKPOINT_LSN,
		LOG_CHECKPOINT_CHECKSUM_2 - LOG_CHECKPOINT_LSN);
	mach_write_to_4(log_buf + LOG_CHECKPOINT_1 + LOG_CHECKPOINT_CHECKSUM_2, fold);

	MACH_WRITE_64(log_buf + LOG_CHECKPOINT_2 + LOG_CHECKPOINT_LSN, max_lsn);
	mach_write_to_4(log_buf + LOG_CHECKPOINT_2
			+ LOG_CHECKPOINT_OFFSET_LOW32,
			LOG_FILE_HDR_SIZE + (ulint) ut_dulint_minus(max_lsn,
			ut_dulint_align_down(max_lsn,OS_FILE_LOG_BLOCK_SIZE)));
#ifdef XTRADB_BASED
	MACH_WRITE_64(log_buf + LOG_CHECKPOINT_2 + LOG_CHECKPOINT_ARCHIVED_LSN,
			(ib_uint64_t)(LOG_FILE_HDR_SIZE + ut_dulint_minus(max_lsn,
					ut_dulint_align_down(max_lsn,OS_FILE_LOG_BLOCK_SIZE))));
#endif
        fold = ut_fold_binary(log_buf + LOG_CHECKPOINT_2, LOG_CHECKPOINT_CHECKSUM_1);
        mach_write_to_4(log_buf + LOG_CHECKPOINT_2 + LOG_CHECKPOINT_CHECKSUM_1, fold);

        fold = ut_fold_binary(log_buf + LOG_CHECKPOINT_2 + LOG_CHECKPOINT_LSN,
                LOG_CHECKPOINT_CHECKSUM_2 - LOG_CHECKPOINT_LSN);
        mach_write_to_4(log_buf + LOG_CHECKPOINT_2 + LOG_CHECKPOINT_CHECKSUM_2, fold);


	success = xb_os_file_write(src_path, src_file, log_buf, 0,
				   LOG_FILE_HDR_SIZE);
	if (!success) {
		goto error;
	}

	/* expand file size (9/8) and align to UNIV_PAGE_SIZE */

	if (file_size % UNIV_PAGE_SIZE) {
		memset(log_buf, 0, UNIV_PAGE_SIZE);
		success = xb_os_file_write(src_path, src_file, log_buf,
					    file_size,
					    UNIV_PAGE_SIZE
					    - (ulint) (file_size
						       % UNIV_PAGE_SIZE));
		if (!success) {
			goto error;
		}

		file_size = os_file_get_size(src_file);
	}

	/* TODO: We should judge whether the file is already expanded or not... */
	{
		ulint	expand;

		memset(log_buf, 0, UNIV_PAGE_SIZE * 128);
		expand = (ulint) (file_size / UNIV_PAGE_SIZE / 8);

		for (; expand > 128; expand -= 128) {
			success = xb_os_file_write(src_path, src_file, log_buf,
						   file_size,
						   UNIV_PAGE_SIZE * 128);
			if (!success) {
				goto error;
			}
			file_size += UNIV_PAGE_SIZE * 128;
		}

		if (expand) {
			success = xb_os_file_write(src_path, src_file, log_buf,
						   file_size,
						   expand * UNIV_PAGE_SIZE);
			if (!success) {
				goto error;
			}
			file_size += UNIV_PAGE_SIZE * expand;
		}
	}

	/* make larger than 2MB */
	if (file_size < 2*1024*1024L) {
		memset(log_buf, 0, UNIV_PAGE_SIZE);
		while (file_size < 2*1024*1024L) {
			success = xb_os_file_write(src_path, src_file, log_buf,
						   file_size, UNIV_PAGE_SIZE);
			if (!success) {
				goto error;
			}
			file_size += UNIV_PAGE_SIZE;
		}
		file_size = os_file_get_size(src_file);
	}

#ifndef INNODB_VERSION_SHORT
	msg("xtrabackup: xtrabackup_logfile detected: size=" INT64PF ", "
	    "start_lsn=(%lu %lu)\n", file_size, max_lsn.high, max_lsn.low);
#else
	msg("xtrabackup: xtrabackup_logfile detected: size=" INT64PF ", "
	    "start_lsn=(" LSN_PF ")\n", file_size, max_lsn);
#endif

	os_file_close(src_file);
	src_file = XB_FILE_UNDEFINED;

	/* Backup log parameters */
	innobase_log_group_home_dir_backup = INNODB_LOG_DIR;
	innobase_log_file_size_backup      = innobase_log_file_size;
	innobase_log_files_in_group_backup = innobase_log_files_in_group;

	/* fake InnoDB */
	INNODB_LOG_DIR = NULL;
	innobase_log_file_size      = file_size;
	innobase_log_files_in_group = 1;

	srv_thread_concurrency = 0;

	/* rename 'xtrabackup_logfile' to 'ib_logfile0' */
	success = xb_file_rename(src_path, dst_path);
	if (!success) {
		goto error;
	}
	xtrabackup_logfile_is_renamed = TRUE;

	ut_free(log_buf_);

	return(FALSE);

skip_modify:
	os_file_close(src_file);
	src_file = XB_FILE_UNDEFINED;
	ut_free(log_buf_);
	return(FALSE);

error:
	if (src_file != XB_FILE_UNDEFINED)
		os_file_close(src_file);
	if (log_buf_)
		ut_free(log_buf_);
	msg("xtrabackup: Error: xtrabackup_init_temp_log() failed.\n");
	return(TRUE); /*ERROR*/
}

/***********************************************************************
Generates path to the meta file path from a given path to an incremental .delta
by replacing trailing ".delta" with ".meta", or returns error if 'delta_path'
does not end with the ".delta" character sequence.
@return TRUE on success, FALSE on error. */
static
ibool
get_meta_path(
	const char	*delta_path,	/* in: path to a .delta file */
	char 		*meta_path)	/* out: path to the corresponding .meta
					file */
{
	size_t		len = strlen(delta_path);

	if (len <= 6 || strcmp(delta_path + len - 6, ".delta")) {
		return FALSE;
	}
	memcpy(meta_path, delta_path, len - 6);
	strcpy(meta_path + len - 6, XB_DELTA_INFO_SUFFIX);

	return TRUE;
}

/****************************************************************//**
Create a new tablespace on disk and return the handle to its opened
file. Code adopted from fil_create_new_single_table_tablespace with
the main difference that only disk file is created without updating
the InnoDB in-memory dictionary data structures.

@return TRUE on success, FALSE on error.  */
static
ibool
xb_delta_create_space_file(
/*=======================*/
	const char*	path,		/*!<in: path to tablespace */
	ulint		space_id,	/*!<in: space id */
	ulint		flags __attribute__((unused)),/*!<in: tablespace
					flags */
	os_file_t*	file)		/*!<out: file handle */
{
	ibool		ret;
	byte*		buf;
	byte*		page;

	*file = xb_file_create_no_error_handling(path, OS_FILE_CREATE,
						 OS_FILE_READ_WRITE, &ret);
	if (!ret) {
		msg("xtrabackup: cannot create file %s\n", path);
		return ret;
	}

	ret = xb_os_file_set_size(path, *file,
				  FIL_IBD_FILE_INITIAL_SIZE * UNIV_PAGE_SIZE);
	if (!ret) {
		msg("xtrabackup: cannot set size for file %s\n", path);
		os_file_close(*file);
		os_file_delete(innodb_file_data_key, path);
		return ret;
	}

	buf = static_cast<byte *>(ut_malloc(3 * UNIV_PAGE_SIZE));
	/* Align the memory for file i/o if we might have O_DIRECT set */
	page = static_cast<byte *>(ut_align(buf, UNIV_PAGE_SIZE));

	memset(page, '\0', UNIV_PAGE_SIZE);

#ifdef INNODB_VERSION_SHORT
	fsp_header_init_fields(page, space_id, flags);
	mach_write_to_4(page + FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID, space_id);

	if (!fsp_flags_is_compressed(flags)) {
		buf_flush_init_for_writing(page, NULL, 0);

		ret = xb_os_file_write(path, *file, page, 0, UNIV_PAGE_SIZE);
	}
	else {
		page_zip_des_t	page_zip;
		ulint		zip_size;

		zip_size = fsp_flags_get_zip_size(flags);
		page_zip_set_size(&page_zip, zip_size);
		page_zip.data = page + UNIV_PAGE_SIZE;
		fprintf(stderr, "zip_size = %lu\n", zip_size);

#ifdef UNIV_DEBUG
		page_zip.m_start =
#endif /* UNIV_DEBUG */
			page_zip.m_end = page_zip.m_nonempty =
			page_zip.n_blobs = 0;

		buf_flush_init_for_writing(page, &page_zip, 0);

		ret = xb_os_file_write(path, *file, page_zip.data, 0,
				       zip_size);
	}
#else
	fsp_header_write_space_id(page, space_id);

	buf_flush_init_for_writing(page, ut_dulint_zero, space_id, 0);

	ret = xb_os_file_write(path, *file, page, 0, UNIV_PAGE_SIZE);
#endif

	ut_free(buf);

	if (!ret) {
		msg("xtrabackup: could not write the first page to %s\n",
		    path);
		os_file_close(*file);
		os_file_delete(innodb_file_data_key, path);
		return ret;
	}

	return TRUE;
}

/***********************************************************************
Searches for matching tablespace file for given .delta file and space_id
in given directory. When matching tablespace found, renames it to match the
name of .delta file. If there was a tablespace with matching name and
mismatching ID, renames it to xtrabackup_tmp_#ID.ibd. If there was no
matching file, creates a new tablespace.
@return file handle of matched or created file */
static
os_file_t
xb_delta_open_matching_space(
	const char*	dbname,		/* in: path to destination database dir */
	const char*	name,		/* in: name of delta file (without .delta) */
	ulint		space_id,	/* in: space id of delta file */
	ulint		zip_size,	/* in: zip_size of tablespace */
	char*		real_name,	/* out: full path of destination file */
	size_t		real_name_len,	/* out: buffer size for real_name */
	ibool* 		success)	/* out: indicates error. TRUE = success */
{
	char		dest_dir[FN_REFLEN];
	char		dest_space_name[FN_REFLEN];
	ibool		ok;
	fil_space_t*	fil_space;
	os_file_t	file	= 0;
	ulint		tablespace_flags;

	ut_a(dbname != NULL ||
		trx_sys_sys_space(space_id) ||
		space_id == ULINT_UNDEFINED);

	*success = FALSE;

	if (dbname) {
		snprintf(dest_dir, FN_REFLEN, "%s/%s",
			xtrabackup_target_dir, dbname);
		srv_normalize_path_for_win(dest_dir);

		snprintf(dest_space_name, FN_REFLEN, "%s%s/%s",
			 xb_dict_prefix, dbname, name);
	} else {
		snprintf(dest_dir, FN_REFLEN, "%s", xtrabackup_target_dir);
		srv_normalize_path_for_win(dest_dir);

		snprintf(dest_space_name, FN_REFLEN, "%s%s", xb_dict_prefix,
			 name);
	}

	snprintf(real_name, real_name_len,
		 "%s/%s",
		 xtrabackup_target_dir, dest_space_name);
	srv_normalize_path_for_win(real_name);
	dest_space_name[strlen(dest_space_name) - XB_DICT_SUFFIX_LEN] = '\0';

	/* Create the database directory if it doesn't exist yet */
	if (!os_file_create_directory(dest_dir, FALSE)) {
		msg("xtrabackup: error: cannot create dir %s\n", dest_dir);
		return file;
	}

	if (trx_sys_sys_space(space_id)) {
		goto found;
	}

	mutex_enter(&fil_system->mutex);
	fil_space = xb_space_get_by_name(dest_space_name);
	mutex_exit(&fil_system->mutex);

	if (fil_space != NULL) {
		if (fil_space->id == space_id || space_id == ULINT_UNDEFINED) {
			/* we found matching space */
			goto found;
		} else {

			char	tmpname[FN_REFLEN];

			snprintf(tmpname, FN_REFLEN, "%s%s/xtrabackup_tmp_#%lu",
				 xb_dict_prefix, dbname, fil_space->id);

			msg("xtrabackup: Renaming %s to %s.ibd\n",
				fil_space->name, tmpname);

			if (!xb_fil_rename_tablespace(NULL, fil_space->id,
						      tmpname, NULL))
			{
				msg("xtrabackup: Cannot rename %s to %s\n",
					fil_space->name, tmpname);
				goto exit;
			}
		}
	}

	mutex_enter(&fil_system->mutex);
	ut_a(space_id != ULINT_UNDEFINED);
	fil_space = xb_space_get_by_id(space_id);
	mutex_exit(&fil_system->mutex);
	if (fil_space != NULL) {
		char	tmpname[FN_REFLEN];

		strncpy(tmpname, dest_space_name, FN_REFLEN);
#if MYSQL_VERSION_ID < 50600
		/* Need to truncate .ibd before renaming.  For MySQL 5.6 it's
		already truncated.  */
		tmpname[strlen(tmpname) - 4] = 0;
#endif

		msg("xtrabackup: Renaming %s to %s\n",
		    fil_space->name, dest_space_name);

		if (!xb_fil_rename_tablespace(NULL, fil_space->id, tmpname,
					      NULL))
		{
			msg("xtrabackup: Cannot rename %s to %s\n",
				fil_space->name, dest_space_name);
			goto exit;
		}

		goto found;
	}

	/* No matching space found. create the new one.  */

#ifdef INNODB_VERSION_SHORT
	if (!fil_space_create(dest_space_name, space_id, 0, FIL_TABLESPACE)) {
#else
	if (!fil_space_create(dest_space_name, space_id, FIL_TABLESPACE)) {
#endif
		msg("xtrabackup: Cannot create tablespace %s\n",
			dest_space_name);
		goto exit;
	}

	/* Calculate correct tablespace flags for compressed tablespaces.  */
	if (!zip_size || zip_size == ULINT_UNDEFINED) {
		tablespace_flags = 0;
	}
	else {
		tablespace_flags
			= (get_bit_shift(zip_size >> PAGE_ZIP_MIN_SIZE_SHIFT
					 << 1)
			   << DICT_TF_ZSSIZE_SHIFT)
			| DICT_TF_COMPACT
			| (DICT_TF_FORMAT_ZIP << DICT_TF_FORMAT_SHIFT);
#ifdef INNODB_VERSION_SHORT
		ut_a(dict_tf_get_zip_size(tablespace_flags)
		     == zip_size);
#endif
	}

	*success = xb_delta_create_space_file(real_name, space_id,
					      tablespace_flags, &file);

	goto exit;

found:
	/* open the file and return it's handle */

	file = xb_file_create_no_error_handling(real_name, OS_FILE_OPEN,
					    OS_FILE_READ_WRITE,
					    &ok);

	if (ok) {
		*success = TRUE;
	} else {
		msg("xtrabackup: Cannot open file %s\n", real_name);
	}

exit:

	return file;
}

/************************************************************************
Applies a given .delta file to the corresponding data file.
@return TRUE on success */
static
ibool
xtrabackup_apply_delta(
	const char*	dirname,	/* in: dir name of incremental */
	const char*	dbname,		/* in: database name (ibdata: NULL) */
	const char*	filename,	/* in: file name (not a path),
					including the .delta extension */
	my_bool check_newer __attribute__((unused)))
{
	os_file_t	src_file = XB_FILE_UNDEFINED;
	os_file_t	dst_file = XB_FILE_UNDEFINED;
	char	src_path[FN_REFLEN];
	char	dst_path[FN_REFLEN];
	char	meta_path[FN_REFLEN];
	char	space_name[FN_REFLEN];
	ibool	success;

	ibool	last_buffer = FALSE;
	ulint	page_in_buffer;
	ulint	incremental_buffers = 0;

	xb_delta_info_t info;
	ulint		page_size;
	ulint		page_size_shift;
	byte*		incremental_buffer_base = NULL;
	byte*		incremental_buffer;

	ut_a(xtrabackup_incremental);

	if (dbname) {
		snprintf(src_path, sizeof(src_path), "%s/%s/%s",
			 dirname, dbname, filename);
		snprintf(dst_path, sizeof(dst_path), "%s/%s/%s",
			 xtrabackup_real_target_dir, dbname, filename);
	} else {
		snprintf(src_path, sizeof(src_path), "%s/%s",
			 dirname, filename);
		snprintf(dst_path, sizeof(dst_path), "%s/%s",
			 xtrabackup_real_target_dir, filename);
	}
	dst_path[strlen(dst_path) - 6] = '\0';

	strncpy(space_name, filename, FN_REFLEN);
	space_name[strlen(space_name) -  6] = 0;

	if (!get_meta_path(src_path, meta_path)) {
		goto error;
	}

	srv_normalize_path_for_win(dst_path);
	srv_normalize_path_for_win(src_path);
	srv_normalize_path_for_win(meta_path);

	if (!xb_read_delta_metadata(meta_path, &info)) {
		goto error;
	}

	page_size = info.page_size;
	page_size_shift = get_bit_shift(page_size);
	msg("xtrabackup: page size for %s is %lu bytes\n",
	    src_path, page_size);
	if (page_size_shift < 10 ||
	    page_size_shift > UNIV_PAGE_SIZE_SHIFT_MAX) {
		msg("xtrabackup: error: invalid value of page_size "
		    "(%lu bytes) read from %s\n", page_size, meta_path);
		goto error;
	}
	
	src_file = xb_file_create_no_error_handling(src_path, OS_FILE_OPEN,
						    OS_FILE_READ_WRITE,
						    &success);
	if (!success) {
		os_file_get_last_error(TRUE);
		msg("xtrabackup: error: cannot open %s\n", src_path);
		goto error;
	}

#ifdef USE_POSIX_FADVISE
	posix_fadvise(src_file, 0, 0, POSIX_FADV_SEQUENTIAL);
	posix_fadvise(src_file, 0, 0, POSIX_FADV_DONTNEED);
#endif

	xb_file_set_nocache(src_file, src_path, "OPEN");

	dst_file = xb_delta_open_matching_space(
			dbname, space_name, info.space_id, info.zip_size,
			dst_path, sizeof(dst_path), &success);
	if (!success) {
		msg("xtrabackup: error: cannot open %s\n", dst_path);
		goto error;
	}

#ifdef USE_POSIX_FADVISE
	posix_fadvise(dst_file, 0, 0, POSIX_FADV_DONTNEED);
#endif

	xb_file_set_nocache(dst_file, dst_path, "OPEN");

	/* allocate buffer for incremental backup (4096 pages) */
	incremental_buffer_base = static_cast<byte *>
		(ut_malloc((UNIV_PAGE_SIZE_MAX / 4 + 1) *
			   UNIV_PAGE_SIZE_MAX));
	incremental_buffer = static_cast<byte *>
		(ut_align(incremental_buffer_base,
			  UNIV_PAGE_SIZE_MAX));

	msg("Applying %s to %s...\n", src_path, dst_path);

	while (!last_buffer) {
		ulint cluster_header;

		/* read to buffer */
		/* first block of block cluster */
		success = xb_os_file_read(src_file, incremental_buffer,
					  ((incremental_buffers
					    * (page_size / 4))
					   << page_size_shift),
					  page_size);
		if (!success) {
			goto error;
		}

		cluster_header = mach_read_from_4(incremental_buffer);
		switch(cluster_header) {
			case 0x78747261UL: /*"xtra"*/
				break;
			case 0x58545241UL: /*"XTRA"*/
				last_buffer = TRUE;
				break;
			default:
				msg("xtrabackup: error: %s seems not "
				    ".delta file.\n", src_path);
				goto error;
		}

		for (page_in_buffer = 1; page_in_buffer < page_size / 4;
		     page_in_buffer++) {
			if (mach_read_from_4(incremental_buffer + page_in_buffer * 4)
			    == 0xFFFFFFFFUL)
				break;
		}

		ut_a(last_buffer || page_in_buffer == page_size / 4);

		/* read whole of the cluster */
		success = xb_os_file_read(src_file, incremental_buffer,
					  ((incremental_buffers
					    * (page_size / 4))
					   << page_size_shift),
					  page_in_buffer * page_size);
		if (!success) {
			goto error;
		}

		for (page_in_buffer = 1; page_in_buffer < page_size / 4;
		     page_in_buffer++) {
			ulint offset_on_page;

			offset_on_page = mach_read_from_4(incremental_buffer + page_in_buffer * 4);

			if (offset_on_page == 0xFFFFFFFFUL)
				break;

			/* apply blocks in the cluster */
//			if (ut_dulint_cmp(incremental_lsn,
//				MACH_READ_64(incremental_buffer
//						 + page_in_buffer * page_size
//						 + FIL_PAGE_LSN)) >= 0)
//				continue;

			success = xb_os_file_write(dst_path, dst_file,
					incremental_buffer +
						page_in_buffer * page_size,
					(offset_on_page << page_size_shift),
					page_size);
			if (!success) {
				goto error;
			}
		}

		incremental_buffers++;
	}

	if (incremental_buffer_base)
		ut_free(incremental_buffer_base);
	if (src_file != XB_FILE_UNDEFINED)
		os_file_close(src_file);
	if (dst_file != XB_FILE_UNDEFINED)
		os_file_close(dst_file);
	return TRUE;

error:
	if (incremental_buffer_base)
		ut_free(incremental_buffer_base);
	if (src_file != XB_FILE_UNDEFINED)
		os_file_close(src_file);
	if (dst_file != XB_FILE_UNDEFINED)
		os_file_close(dst_file);
	msg("xtrabackup: Error: xtrabackup_apply_delta(): "
	    "failed to apply %s to %s.\n", src_path, dst_path);
	return FALSE;
}

/************************************************************************
Applies all .delta files from incremental_dir to the full backup.
@return TRUE on success. */
static
ibool
xtrabackup_apply_deltas(my_bool check_newer)
{
	ulint		ret;
	char		dbpath[FN_REFLEN];
	os_file_dir_t	dir;
	os_file_dir_t	dbdir;
	os_file_stat_t	dbinfo;
	os_file_stat_t	fileinfo;
	dberr_t		err		= DB_SUCCESS;
	static char	current_dir[2];

	current_dir[0] = FN_CURLIB;
	current_dir[1] = 0;
	srv_data_home = current_dir;

	/* datafile */
	dbdir = os_file_opendir(xtrabackup_incremental_dir, FALSE);

	if (dbdir != NULL) {
		ret = fil_file_readdir_next_file(&err, xtrabackup_incremental_dir, dbdir,
							&fileinfo);
		while (ret == 0) {
			if (fileinfo.type == OS_FILE_TYPE_DIR) {
				goto next_file_item_1;
			}

			if (strlen(fileinfo.name) > 6
			    && 0 == strcmp(fileinfo.name + 
					strlen(fileinfo.name) - 6,
					".delta")) {
				if (!xtrabackup_apply_delta(
					    xtrabackup_incremental_dir, NULL,
					    fileinfo.name, check_newer))
				{
					return FALSE;
				}
			}
next_file_item_1:
			ret = fil_file_readdir_next_file(&err,
							xtrabackup_incremental_dir, dbdir,
							&fileinfo);
		}

		os_file_closedir(dbdir);
	} else {
		msg("xtrabackup: Cannot open dir %s\n",
		    xtrabackup_incremental_dir);
	}

	/* single table tablespaces */
	dir = os_file_opendir(xtrabackup_incremental_dir, FALSE);

	if (dir == NULL) {
		msg("xtrabackup: Cannot open dir %s\n",
		    xtrabackup_incremental_dir);
	}

		ret = fil_file_readdir_next_file(&err, xtrabackup_incremental_dir, dir,
								&dbinfo);
	while (ret == 0) {
		if (dbinfo.type == OS_FILE_TYPE_FILE
		    || dbinfo.type == OS_FILE_TYPE_UNKNOWN) {

		        goto next_datadir_item;
		}

		sprintf(dbpath, "%s/%s", xtrabackup_incremental_dir,
								dbinfo.name);
		srv_normalize_path_for_win(dbpath);

		dbdir = os_file_opendir(dbpath, FALSE);

		if (dbdir != NULL) {

			ret = fil_file_readdir_next_file(&err, dbpath, dbdir,
								&fileinfo);
			while (ret == 0) {

			        if (fileinfo.type == OS_FILE_TYPE_DIR) {

				        goto next_file_item_2;
				}

				if (strlen(fileinfo.name) > 6
				    && 0 == strcmp(fileinfo.name + 
						strlen(fileinfo.name) - 6,
						".delta")) {
					/* The name ends in .delta; try opening
					the file */
					if (!xtrabackup_apply_delta(
						    xtrabackup_incremental_dir,
						    dbinfo.name,
						    fileinfo.name, check_newer))
					{
						return FALSE;
					}
				}
next_file_item_2:
				ret = fil_file_readdir_next_file(&err,
								dbpath, dbdir,
								&fileinfo);
			}

			os_file_closedir(dbdir);
		}
next_datadir_item:
		ret = fil_file_readdir_next_file(&err,
						xtrabackup_incremental_dir,
								dir, &dbinfo);
	}

	os_file_closedir(dir);

	return TRUE;
}

static my_bool
xtrabackup_close_temp_log(my_bool clear_flag)
{
	os_file_t	src_file = XB_FILE_UNDEFINED;
	char	src_path[FN_REFLEN];
	char	dst_path[FN_REFLEN];
	ibool	success;

	byte*	log_buf;
	byte*	log_buf_ = NULL;


	if (!xtrabackup_logfile_is_renamed)
		return(FALSE);

	/* Restore log parameters */
	INNODB_LOG_DIR = innobase_log_group_home_dir_backup;
	innobase_log_file_size      = innobase_log_file_size_backup;
	innobase_log_files_in_group = innobase_log_files_in_group_backup;

	/* rename 'ib_logfile0' to 'xtrabackup_logfile' */
	if(!xtrabackup_incremental_dir) {
		sprintf(dst_path, "%s/ib_logfile0", xtrabackup_target_dir);
		sprintf(src_path, "%s/%s", xtrabackup_target_dir,
			XB_LOG_FILENAME);
	} else {
		sprintf(dst_path, "%s/ib_logfile0", xtrabackup_incremental_dir);
		sprintf(src_path, "%s/%s", xtrabackup_incremental_dir,
			XB_LOG_FILENAME);
	}

	srv_normalize_path_for_win(dst_path);
	srv_normalize_path_for_win(src_path);

	success = xb_file_rename(dst_path, src_path);
	if (!success) {
		goto error;
	}
	xtrabackup_logfile_is_renamed = FALSE;

	if (!clear_flag)
		return(FALSE);

	/* clear LOG_FILE_WAS_CREATED_BY_HOT_BACKUP field */
	src_file = xb_file_create_no_error_handling(src_path, OS_FILE_OPEN,
						    OS_FILE_READ_WRITE,
						    &success);
	if (!success) {
		goto error;
	}

#ifdef USE_POSIX_FADVISE
	posix_fadvise(src_file, 0, 0, POSIX_FADV_DONTNEED);
#endif

	log_buf_ = static_cast<byte *>(ut_malloc(LOG_FILE_HDR_SIZE * 2));
	log_buf = static_cast<byte *>(ut_align(log_buf_, LOG_FILE_HDR_SIZE));

	success = xb_os_file_read(src_file, log_buf, 0, LOG_FILE_HDR_SIZE);
	if (!success) {
		goto error;
	}

	memset(log_buf + LOG_FILE_WAS_CREATED_BY_HOT_BACKUP, ' ', 4);

	success = xb_os_file_write(src_path, src_file, log_buf, 0,
				   LOG_FILE_HDR_SIZE);
	if (!success) {
		goto error;
	}

	os_file_close(src_file);
	src_file = XB_FILE_UNDEFINED;

	return(FALSE);
error:
	if (src_file != XB_FILE_UNDEFINED)
		os_file_close(src_file);
	if (log_buf_)
		ut_free(log_buf_);
	msg("xtrabackup: Error: xtrabackup_close_temp_log() failed.\n");
	return(TRUE); /*ERROR*/
}

static void
xtrabackup_prepare_func(void)
{
	ulint	err;

	/* cd to target-dir */

	if (my_setwd(xtrabackup_real_target_dir,MYF(MY_WME)))
	{
		msg("xtrabackup: cannot my_setwd %s\n",
		    xtrabackup_real_target_dir);
		exit(EXIT_FAILURE);
	}
	msg("xtrabackup: cd to %s\n", xtrabackup_real_target_dir);

	xtrabackup_target_dir= mysql_data_home_buff;
	xtrabackup_target_dir[0]=FN_CURLIB;		// all paths are relative from here
	xtrabackup_target_dir[1]=0;

	/* read metadata of target */
	{
		char	filename[FN_REFLEN];

		sprintf(filename, "%s/%s", xtrabackup_target_dir, XTRABACKUP_METADATA_FILENAME);

		if (!xtrabackup_read_metadata(filename))
			msg("xtrabackup: error: xtrabackup_read_metadata()\n");

		if (!strcmp(metadata_type, "full-backuped")) {
			msg("xtrabackup: This target seems to be not prepared "
			    "yet.\n");
		} else if (!strcmp(metadata_type, "full-prepared")) {
			msg("xtrabackup: This target seems to be already "
			    "prepared.\n");
			goto skip_check;
		} else {
			msg("xtrabackup: This target seems not to have correct "
			    "metadata...\n");
		}

		if (xtrabackup_incremental) {
			msg("xtrabackup: error: applying incremental backup "
			    "needs target prepared.\n");
			exit(EXIT_FAILURE);
		}
skip_check:
		if (xtrabackup_incremental
		    && ut_dulint_cmp(metadata_to_lsn, incremental_lsn) != 0) {
			msg("xtrabackup: error: This incremental backup seems "
			    "not to be proper for the target.\n"
			    "xtrabackup:  Check 'to_lsn' of the target and "
			    "'from_lsn' of the incremental.\n");
			exit(EXIT_FAILURE);
		}
	}

	/* Create logfiles for recovery from 'xtrabackup_logfile', before start InnoDB */
	srv_max_n_threads = 1000;
	os_sync_mutex = NULL;
#ifdef INNODB_VERSION_SHORT
	ut_mem_init();
#ifdef XTRADB_BASED
	/* temporally dummy value to avoid crash */
	srv_page_size_shift = 14;
	srv_page_size = (1 << srv_page_size_shift);
#endif
#else /* INNODB_VERSION_SHORT */
	ut_mem_block_list_init();
#endif
	os_sync_init();
	sync_init();
	os_io_init_simple();
	if(xtrabackup_init_temp_log())
		goto error;

	if(innodb_init_param())
		goto error;

	xb_normalize_init_values();

#if MYSQL_VERSION_ID >= 50600
	ut_crc32_init();
#endif

	mem_init(srv_mem_pool_size);
	if (xtrabackup_incremental) {
		err = xb_data_files_init();
		if (err != DB_SUCCESS) {
			msg("xtrabackup: error: xb_data_files_init() failed with"
			    "error code %lu\n", err);
			goto error;
		}

		if(!xtrabackup_apply_deltas(TRUE)) {
			xb_data_files_close();
			goto error;
		}

		xb_data_files_close();
	}
	sync_close();
	sync_initialized = FALSE;
	os_sync_free();
	mem_close();
	os_sync_mutex = NULL;
	ut_free_all_mem();

	/* check the accessibility of target-dir */
	/* ############# TODO ##################### */

	if(innodb_init_param())
		goto error;

	srv_apply_log_only = (ibool) xtrabackup_apply_log_only;

	/* increase IO threads */
	if(srv_n_file_io_threads < 10) {
#ifndef INNODB_VERSION_SHORT
		srv_n_file_io_threads = 10;
#else
		srv_n_read_io_threads = 4;
		srv_n_write_io_threads = 4;
#endif
	}

	msg("xtrabackup: Starting InnoDB instance for recovery.\n"
	    "xtrabackup: Using %lld bytes for buffer pool "
	    "(set by --use-memory parameter)\n", xtrabackup_use_memory);

	if(innodb_init())
		goto error;

	/* align space sizes along with fsp header */
	{
	fil_system_t*	f_system = fil_system;
	fil_space_t*	space;

	mutex_enter(&(f_system->mutex));
	space = UT_LIST_GET_FIRST(f_system->space_list);

	while (space != NULL) {
		byte*	header;
		ulint	size;
		ulint	actual_size;
		mtr_t	mtr;
#ifdef INNODB_VERSION_SHORT
		buf_block_t*	block;
		ulint	flags;
#endif

		if (space->purpose == FIL_TABLESPACE) {
			mutex_exit(&(f_system->mutex));

			mtr_start(&mtr);

#ifndef INNODB_VERSION_SHORT
			mtr_s_lock(fil_space_get_latch(space->id), &mtr);

			header = buf_page_get(space->id, 0, RW_S_LATCH, &mtr);
#ifdef UNIV_SYNC_DEBUG
			buf_page_dbg_add_level(header,
					       SYNC_NO_ORDER_CHECK);
#endif
			header += FIL_PAGE_DATA;
#else
			mtr_s_lock(fil_space_get_latch(space->id, &flags), &mtr);

			block = buf_page_get(space->id,
					     dict_tf_get_zip_size(flags),
					     0, RW_S_LATCH, &mtr);
			header = FIL_PAGE_DATA /*FSP_HEADER_OFFSET*/
				+ buf_block_get_frame(block);
#endif

			size = mtr_read_ulint(header + 8 /* FSP_SIZE */, MLOG_4BYTES, &mtr);

			mtr_commit(&mtr);

			fil_extend_space_to_desired_size(&actual_size, space->id, size);

			mutex_enter(&(f_system->mutex));
		}

		space = UT_LIST_GET_NEXT(space_list, space);
	}

	mutex_exit(&(f_system->mutex));
	}



	if (xtrabackup_export) {
		msg("xtrabackup: export option is specified.\n");
		if (innobase_file_per_table) {
			fil_system_t*	f_system = fil_system;
			fil_space_t*	space;
			fil_node_t*	node;
			os_file_t	info_file = XB_FILE_UNDEFINED;
			char		info_file_path[FN_REFLEN];
			ibool		success;
			char		table_name[FN_REFLEN];

			byte*		page;
			byte*		buf = NULL;

			buf = static_cast<byte *>
				(ut_malloc(UNIV_PAGE_SIZE * 2));
			page = static_cast<byte *>
				(ut_align(buf, UNIV_PAGE_SIZE));

			/* flush insert buffer at shutdwon */
			innobase_fast_shutdown = 0;

			mutex_enter(&(f_system->mutex));

			space = UT_LIST_GET_FIRST(f_system->space_list);
			while (space != NULL) {
				/* treat file_per_table only */
				if (space->purpose != FIL_TABLESPACE
				    || trx_sys_sys_space(space->id)
				   )
				{
					space = UT_LIST_GET_NEXT(space_list, space);
					continue;
				}

				node = UT_LIST_GET_FIRST(space->chain);
				while (node != NULL) {
					int len;
					char *next, *prev, *p;
					dict_table_t*	table;
					dict_index_t*	index;
					ulint		n_index;

					/* node exist == file exist, here */
					strncpy(info_file_path, node->name, FN_REFLEN);
					len = strlen(info_file_path);
					info_file_path[len - 3] = 'e';
					info_file_path[len - 2] = 'x';
					info_file_path[len - 1] = 'p';

					p = info_file_path;
					prev = NULL;
					while ((next = strstr(p, SRV_PATH_SEPARATOR_STR)) != NULL)
					{
						prev = p;
						p = next + 1;
					}
					info_file_path[len - 4] = 0;
					strncpy(table_name, prev, FN_REFLEN);

					info_file_path[len - 4] = '.';

					mutex_exit(&(f_system->mutex));
					mutex_enter(&(dict_sys->mutex));

					table = dict_table_get_low(table_name);
					if (!table) {
						msg("xtrabackup: error: "
						    "cannot find dictionary "
						    "record of table %s\n",
						    table_name);
						goto next_node;
					}
					index = dict_table_get_first_index(table);
					n_index = UT_LIST_GET_LEN(table->indexes);
					if (n_index > 31) {
						msg("xtrabackup: error: "
						    "sorry, cannot export over "
						    "31 indexes for now.\n");
						goto next_node;
					}

					/* init exp file */
					memset(page, 0, UNIV_PAGE_SIZE);
					mach_write_to_4(page    , 0x78706f72UL);
					mach_write_to_4(page + 4, 0x74696e66UL);/*"xportinf"*/
					mach_write_to_4(page + 8, n_index);
					strncpy((char *) page + 12,
						table_name, 500);

					msg("xtrabackup: export metadata of "
					    "table '%s' to file `%s` "
					    "(%lu indexes)\n",
					    table_name, info_file_path,
					    n_index);

					n_index = 1;
					while (index) {
						mach_write_to_8(page + n_index * 512, index->id);
						mach_write_to_4(page + n_index * 512 + 8,
#if (MYSQL_VERSION_ID < 50100)
								index->tree->page);
#else /* MYSQL_VERSION_ID < 51000 */
								index->page);
#endif
					strncpy((char *) page + n_index * 512 +
						12, index->name, 500);

						msg("xtrabackup:     name=%s, "
						    "id.low=%lu, page=%lu\n",
						    index->name,
#if (MYSQL_VERSION_ID < 50500)
						    index->id.low,
#else
						    (ulint)(index->id &
							    0xFFFFFFFFUL),
#endif
#if (MYSQL_VERSION_ID < 50100)
						    index->tree->page
#else /* MYSQL_VERSION_ID < 51000 */
						(ulint) index->page
#endif
						);
						index = dict_table_get_next_index(index);
						n_index++;
					}

					srv_normalize_path_for_win(info_file_path);
					info_file = xb_file_create(
						info_file_path,
						OS_FILE_OVERWRITE,
						OS_FILE_NORMAL, OS_DATA_FILE,
						&success);
					if (!success) {
						os_file_get_last_error(TRUE);
						goto next_node;
					}
					success = xb_os_file_write(info_file_path,
								   info_file,
								   page,
								   0,
								   UNIV_PAGE_SIZE);
					if (!success) {
						os_file_get_last_error(TRUE);
						goto next_node;
					}
					success = xb_file_flush(info_file);
					if (!success) {
						os_file_get_last_error(TRUE);
						goto next_node;
					}
next_node:
					if (info_file != XB_FILE_UNDEFINED) {
						os_file_close(info_file);
						info_file = XB_FILE_UNDEFINED;
					}
					mutex_exit(&(dict_sys->mutex));
					mutex_enter(&(f_system->mutex));

					node = UT_LIST_GET_NEXT(chain, node);
				}

				space = UT_LIST_GET_NEXT(space_list, space);
			}
			mutex_exit(&(f_system->mutex));

			ut_free(buf);
		}
	}

	/* print binlog position (again?) */
	msg("\n[notice (again)]\n"
	    "  If you use binary log and don't use any hack of group commit,\n"
	    "  the binary log position seems to be:\n");
	trx_sys_print_mysql_binlog_offset();
	msg("\n");

	/* output to xtrabackup_binlog_pos_innodb */
	if (*trx_sys_mysql_bin_log_name != '\0') {
		FILE *fp;

		fp = fopen("xtrabackup_binlog_pos_innodb", "w");
		if (fp) {
			/* Use UINT64PF instead of LSN_PF here, as we have to
			maintain the file format. */
			fprintf(fp, "%s\t" UINT64PF "\n",
				trx_sys_mysql_bin_log_name,
				trx_sys_mysql_bin_log_pos);
			fclose(fp);
		} else {
			msg("xtrabackup: failed to open "
			    "'xtrabackup_binlog_pos_innodb'\n");
		}
	}

	/* Check whether the log is applied enough or not. */
	if ((xtrabackup_incremental
	     && ut_dulint_cmp(srv_start_lsn, incremental_last_lsn) < 0)
	    ||(!xtrabackup_incremental
	       && ut_dulint_cmp(srv_start_lsn, metadata_last_lsn) < 0)) {
		msg(
"xtrabackup: ########################################################\n"
"xtrabackup: # !!WARNING!!                                          #\n"
"xtrabackup: # The transaction log file is corrupted.               #\n"
"xtrabackup: # The log was not applied to the intended LSN!         #\n"
"xtrabackup: ########################################################\n"
		    );
		if (xtrabackup_incremental) {
#ifndef INNODB_VERSION_SHORT
			msg("xtrabackup: The intended lsn is %lu:%lu\n",
			    incremental_last_lsn.high,
			    incremental_last_lsn.low);
#else
			msg("xtrabackup: The intended lsn is " LSN_PF "\n",
			    incremental_last_lsn);
#endif
		} else {
#ifndef INNODB_VERSION_SHORT
			msg("xtrabackup: The intended lsn is %lu:%lu\n",
			    metadata_last_lsn.high, metadata_last_lsn.low);
#else
			msg("xtrabackup: The intended lsn is " LSN_PF "\n",
			    metadata_last_lsn);
#endif
		}
	}

	if(innodb_end())
		goto error;

	sync_initialized = FALSE;
	os_sync_mutex = NULL;

	/* re-init necessary components */
#ifdef INNODB_VERSION_SHORT
	ut_mem_init();
#else
	ut_mem_block_list_init();
#endif
	os_sync_init();
	sync_init();
	os_io_init_simple();

	if(xtrabackup_close_temp_log(TRUE))
		exit(EXIT_FAILURE);

	/* output to metadata file */
	{
		char	filename[FN_REFLEN];

		strcpy(metadata_type, "full-prepared");

		if(xtrabackup_incremental
		   && ut_dulint_cmp(metadata_to_lsn, incremental_to_lsn) < 0)
		{
			metadata_to_lsn = incremental_to_lsn;
			metadata_last_lsn = incremental_last_lsn;
		}

		sprintf(filename, "%s/%s", xtrabackup_target_dir, XTRABACKUP_METADATA_FILENAME);
		if (!xtrabackup_write_metadata(filename))
			msg("xtrabackup: error: xtrabackup_write_metadata"
			    "(xtrabackup_target_dir)\n");

		if(xtrabackup_extra_lsndir) {
			sprintf(filename, "%s/%s", xtrabackup_extra_lsndir, XTRABACKUP_METADATA_FILENAME);
			if (!xtrabackup_write_metadata(filename))
				msg("xtrabackup: error: "
				    "xtrabackup_write_metadata"
				    "(xtrabackup_extra_lsndir)\n");
		}
	}

	if(!xtrabackup_create_ib_logfile)
		return;

	/* TODO: make more smart */

	msg("\n[notice]\n"
	    "We cannot call InnoDB second time during the process lifetime.\n");
	msg("Please re-execte to create ib_logfile*. Sorry.\n");

	return;

error:
	xtrabackup_close_temp_log(FALSE);

	exit(EXIT_FAILURE);
}

/* ================= main =================== */

int main(int argc, char **argv)
{
	int ho_error;

	MY_INIT(argv[0]);
	xb_regex_init();

#if MYSQL_VERSION_ID >= 50600
	system_charset_info= &my_charset_utf8_general_ci;
	key_map_full.set_all();
#endif

	/* scan options for group to load defaults from */
	{
		int	i;
		char*	optend;
		for (i=1; i < argc; i++) {
			optend = strcend(argv[i], '=');
			if (strncmp(argv[i], "--defaults-group", optend - argv[i]) == 0) {
				xb_load_default_groups[2] = defaults_group = optend + 1;
			}
		}
	}
	load_defaults("my", xb_load_default_groups, &argc, &argv);

	/* ignore unsupported options */
	{
	int i,j,argc_new,find;
	char *optend, *prev_found = NULL;
	argc_new = argc;

	j=1;
	for (i=1 ; i < argc ; i++) {
		uint count;
		struct my_option *opt= (struct my_option *) xb_long_options;
		optend= strcend((argv)[i], '=');
		if (!strncmp(argv[i], "--defaults-file", optend - argv[i]))
		{
			msg("xtrabackup: Error: --defaults-file "
			    "must be specified first on the command "
			    "line\n");
			exit(EXIT_FAILURE);
		}
		for (count= 0; opt->name; opt++) {
			if (!getopt_compare_strings(opt->name, (argv)[i] + 2,
				(uint)(optend - (argv)[i] - 2))) /* match found */
			{
				if (!opt->name[(uint)(optend - (argv)[i] - 2)]) {
					find = 1;
					goto next_opt;
				}
				if (!count) {
					count= 1;
					prev_found= (char *) opt->name;
				}
				else if (strcmp(prev_found, opt->name)) {
					count++;
				}
			}
		}
		find = count;
next_opt:
		if(!find){
			argc_new--;
		} else {
			(argv)[j]=(argv)[i];
			j++;
		}
	}
	argc = argc_new;
	argv[argc] = NULL;
	}

	if ((ho_error=handle_options(&argc, &argv, xb_long_options, get_one_option)))
		exit(ho_error);

	if ((!xtrabackup_print_param) && (!xtrabackup_prepare) && (strcmp(mysql_data_home, "./") == 0)) {
		if (!xtrabackup_print_param)
			usage();
		msg("\nxtrabackup: Error: Please set parameter 'datadir'\n");
		exit(EXIT_FAILURE);
	}

	/* Ensure target dir is not relative to datadir */
	my_load_path(xtrabackup_real_target_dir, xtrabackup_target_dir, NULL);
	xtrabackup_target_dir= xtrabackup_real_target_dir;

#ifdef XTRADB_BASED
	/* temporary setting of enough size */
	srv_page_size_shift = UNIV_PAGE_SIZE_SHIFT_MAX;
	srv_page_size = UNIV_PAGE_SIZE_MAX;
	srv_log_block_size = 512;
#endif
	if (xtrabackup_backup && xtrabackup_incremental) {
		/* direct specification is only for --backup */
		/* and the lsn is prior to the other option */

		char* endchar;
		int error = 0;
#ifndef INNODB_VERSION_SHORT
		char* incremental_low;
		long long lsn_high, lsn_low;

		incremental_low = strstr(xtrabackup_incremental, ":");
		if (incremental_low) {
			*incremental_low = '\0';

			lsn_high = strtoll(xtrabackup_incremental, &endchar, 10);
			if (*endchar != '\0' || (lsn_high >> 32))
				error = 1;

			*incremental_low = ':';
			incremental_low++;

			lsn_low = strtoll(incremental_low, &endchar, 10);

			if (*endchar != '\0' || (lsn_low >> 32))
				error = 1;

			incremental_lsn = ut_dulint_create((ulint)lsn_high, (ulint)lsn_low);
		} else {
			error = 1;
		}
#else
		incremental_lsn = strtoll(xtrabackup_incremental, &endchar, 10);
		if (*endchar != '\0')
			error = 1;
#endif

		if (error) {
			msg("xtrabackup: value '%s' may be wrong format for "
			    "incremental option.\n", xtrabackup_incremental);
			exit(EXIT_FAILURE);
		}
	} else if (xtrabackup_backup && xtrabackup_incremental_basedir) {
		char	filename[FN_REFLEN];

		sprintf(filename, "%s/%s", xtrabackup_incremental_basedir, XTRABACKUP_METADATA_FILENAME);

		if (!xtrabackup_read_metadata(filename)) {
			msg("xtrabackup: error: failed to read metadata from "
			    "%s\n", filename);
			exit(EXIT_FAILURE);
		}

		incremental_lsn = metadata_to_lsn;
		xtrabackup_incremental = xtrabackup_incremental_basedir; //dummy
	} else if (xtrabackup_prepare && xtrabackup_incremental_dir) {
		char	filename[FN_REFLEN];

		sprintf(filename, "%s/%s", xtrabackup_incremental_dir, XTRABACKUP_METADATA_FILENAME);

		if (!xtrabackup_read_metadata(filename)) {
			msg("xtrabackup: error: failed to read metadata from "
			    "%s\n", filename);
			exit(EXIT_FAILURE);
		}

		incremental_lsn = metadata_from_lsn;
		incremental_to_lsn = metadata_to_lsn;
		incremental_last_lsn = metadata_last_lsn;
		xtrabackup_incremental = xtrabackup_incremental_dir; //dummy

	} else {
		xtrabackup_incremental = NULL;
	}

	/* --print-param */
	if (xtrabackup_print_param) {
		/* === some variables from mysqld === */
		memset((G_PTR) &mysql_tmpdir_list, 0,
		       sizeof(mysql_tmpdir_list));

		if (init_tmpdir(&mysql_tmpdir_list, opt_mysql_tmpdir))
			exit(EXIT_FAILURE);

		printf("# This MySQL options file was generated by XtraBackup.\n");
		printf("[%s]\n", defaults_group);
		printf("datadir = \"%s\"\n", mysql_data_home);
		printf("tmpdir = \"%s\"\n", mysql_tmpdir_list.list[0]);
		printf("innodb_data_home_dir = \"%s\"\n",
			innobase_data_home_dir ? innobase_data_home_dir : mysql_data_home);
		printf("innodb_data_file_path = \"%s\"\n",
			innobase_data_file_path ? innobase_data_file_path : "ibdata1:10M:autoextend");
		printf("innodb_log_group_home_dir = \"%s\"\n",
			INNODB_LOG_DIR ? INNODB_LOG_DIR : mysql_data_home);
		printf("innodb_log_files_in_group = %ld\n", innobase_log_files_in_group);
		printf("innodb_log_file_size = %lld\n", innobase_log_file_size);
		printf("innodb_flush_method = \"%s\"\n",
		       (innobase_unix_file_flush_method != NULL) ?
		       innobase_unix_file_flush_method : "");
#ifdef XTRADB_BASED
		printf("innodb_fast_checksum = %d\n", innobase_fast_checksum);
		printf("innodb_page_size = %ld\n", innobase_page_size);
		printf("innodb_log_block_size = %lu\n", innobase_log_block_size);
		if (innobase_doublewrite_file != NULL) {
			printf("innodb_doublewrite_file = %s\n", innobase_doublewrite_file);
		}
#endif
		exit(EXIT_SUCCESS);
	}

	print_version();
	if (!xtrabackup_log_only) {
		if (xtrabackup_incremental) {
#ifndef INNODB_VERSION_SHORT
			msg("incremental backup from %lu:%lu is enabled.\n",
			    incremental_lsn.high, incremental_lsn.low);
#else
			msg("incremental backup from " LSN_PF
			    " is enabled.\n",
			    incremental_lsn);
#endif
		}
	} else {
		if (xtrabackup_backup) {
			xtrabackup_suspend_at_end = TRUE;
			msg("xtrabackup: suspend-at-end is enabled.\n");
		}
	}

	if (xtrabackup_export && innobase_file_per_table == FALSE) {
		msg("xtrabackup: error: --export option can only "
		    "be used with --innodb-file-per-table=ON.\n");
		exit(EXIT_FAILURE);
	}

	if (xtrabackup_incremental && xtrabackup_stream &&
	    xtrabackup_stream_fmt == XB_STREAM_FMT_TAR) {
		msg("xtrabackup: error: "
		    "streaming incremental backups are incompatible with the \n"
		    "'tar' streaming format. Use --stream=xbstream instead.\n");
		exit(EXIT_FAILURE);
	}

	if (xtrabackup_compress && xtrabackup_stream &&
	    xtrabackup_stream_fmt == XB_STREAM_FMT_TAR) {
		msg("xtrabackup: error: "
		    "compressed backups are incompatible with the \n"
		    "'tar' streaming format. Use --stream=xbstream instead.\n");
		exit(EXIT_FAILURE);
	}

	/* cannot execute both for now */
	{
		int num = 0;

		if (xtrabackup_backup) num++;
		if (xtrabackup_stats) num++;
		if (xtrabackup_prepare) num++;
		if (num != 1) { /* !XOR (for now) */
			usage();
			exit(EXIT_FAILURE);
		}
	}

#ifndef __WIN__
	if (xtrabackup_debug_sync) {
		signal(SIGCONT, sigcont_handler);
	}
#endif

	/* --backup */
	if (xtrabackup_backup)
		xtrabackup_backup_func();

	/* --stats */
	if (xtrabackup_stats)
		xtrabackup_stats_func();

	/* --prepare */
	if (xtrabackup_prepare)
		xtrabackup_prepare_func();

	if (xtrabackup_tables) {
		/* free regexp */
		int i;

		for (i = 0; i < tables_regex_num; i++) {
			xb_regfree(&tables_regex[i]);
		}
		ut_free(tables_regex);
	}

	xb_regex_end();

	exit(EXIT_SUCCESS);
}
