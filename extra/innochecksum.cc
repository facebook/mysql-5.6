/*
   Copyright (c) 2005, 2013, Oracle and/or its affiliates. All rights reserved.

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

/*
  InnoDB offline file checksum utility.  85% of the code in this utility
  is included from the InnoDB codebase.

  The final 15% was originally written by Mark Smith of Danga
  Interactive, Inc. <junior@danga.com>

  Published with a permission.
*/

#include <my_config.h>
#include <my_global.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef __WIN__
# include <unistd.h>
#endif
#include <my_getopt.h>
#include <m_string.h>
#include <welcome_copyright_notice.h> /* ORACLE_WELCOME_COPYRIGHT_NOTICE */
#include <string.h>

/* Only parts of these files are included from the InnoDB codebase.
The parts not included are excluded by #ifndef UNIV_INNOCHECKSUM. */

#include "univ.i"                /*  include all of this */

#define FLST_NODE_SIZE (2 * FIL_ADDR_SIZE)
#define FSEG_PAGE_DATA FIL_PAGE_DATA

#include "ut0ut.h"
#include "ut0byte.h"
#include "mach0data.h"
#include "fsp0types.h"
#include "rem0rec.h"
#include "buf0checksum.h"        /* buf_calc_page_*() */
#include "fil0fil.h"             /* FIL_* */
#include "page0page.h"           /* PAGE_* */
#include "page0zip.h"            /* page_zip_*() */
#include "trx0undo.h"            /* TRX_* */
#include "fsp0fsp.h"             /* fsp_flags_get_page_size() &
                                    fsp_flags_get_zip_size() */
#include "mach0data.h"           /* mach_read_from_4() */
#include "ut0crc32.h"            /* ut_crc32_init() */

#ifdef UNIV_NONINL
# include "fsp0fsp.ic"
# include "mach0data.ic"
# include "ut0rnd.ic"
#endif

#undef max
#undef min

#include <unordered_map>

/* Global variables */
static my_bool verbose;
static my_bool debug;
static my_bool skip_corrupt;
static my_bool just_count;
static ullint start_page;
static ullint end_page;
static ullint do_page;
static my_bool use_end_page;
static my_bool do_one_page;
static my_bool per_page_details;
ulong srv_page_size;              /* replaces declaration in srv0srv.c */
static ulong physical_page_size;  /* Page size in bytes on disk. */
static ulong logical_page_size;   /* Page size when uncompressed. */

int n_undo_state_active;
int n_undo_state_cached;
int n_undo_state_to_free;
int n_undo_state_to_purge;
int n_undo_state_prepared;
int n_undo_state_other;
int n_undo_insert, n_undo_update, n_undo_other;
int n_bad_checksum;
int n_fil_page_index;
int n_fil_page_undo_log;
int n_fil_page_inode;
int n_fil_page_ibuf_free_list;
int n_fil_page_allocated;
int n_fil_page_ibuf_bitmap;
int n_fil_page_type_sys;
int n_fil_page_type_trx_sys;
int n_fil_page_type_fsp_hdr;
int n_fil_page_type_allocated;
int n_fil_page_type_xdes;
int n_fil_page_type_blob;
int n_fil_page_type_zblob;
int n_fil_page_type_other;

int n_fil_page_max_index_id;

#define SIZE_RANGES_FOR_PAGE 10
#define NUM_RETRIES 3
#define DEFAULT_RETRY_DELAY 1000000

struct per_index_stats {
  unsigned long long pages;
  unsigned long long leaf_pages;
  unsigned long long total_n_recs;
  unsigned long long total_data_bytes;

  /*!< first element for empty pages,
  last element for pages with more than logical_page_size */
  unsigned long long pages_in_size_range[SIZE_RANGES_FOR_PAGE+2];

  per_index_stats():pages(0), leaf_pages(0), total_n_recs(0),
                   total_data_bytes(0)
  {
    memset(pages_in_size_range, 0, sizeof(pages_in_size_range));
  }
};

std::unordered_map<unsigned long long, per_index_stats> index_ids;

/* Get the page size of the filespace from the filespace header. */
static
my_bool
get_page_size(
/*==========*/
  FILE*  f,                     /*!< in: file pointer, must be open
                                         and set to start of file */
  byte* buf,                    /*!< in: buffer used to read the page */
  ulong* logical_page_size,     /*!< out: Logical/Uncompressed page size */
  ulong* physical_page_size,    /*!< out: Physical/Commpressed page size */
  bool* compressed)             /*!< out: whether the tablespace is
                                compressed */
{
  ulong flags;

  ulong bytes= ulong(fread(buf, 1, UNIV_PAGE_SIZE_MIN, f));

  if (ferror(f))
  {
    perror("Error reading file header");
    return FALSE;
  }

  if (bytes != UNIV_PAGE_SIZE_MIN)
  {
    fprintf(stderr, "Error; Was not able to read the minimum page size ");
    fprintf(stderr, "of %d bytes.  Bytes read was %lu\n", UNIV_PAGE_SIZE_MIN, bytes);
    return FALSE;
  }

  rewind(f);

  flags = mach_read_from_4(buf + FIL_PAGE_DATA + FSP_SPACE_FLAGS);

  /* srv_page_size is used by InnoDB code as UNIV_PAGE_SIZE */
  srv_page_size = *logical_page_size = fsp_flags_get_page_size(flags);

  /* fsp_flags_get_zip_size() will return zero if not compressed. */
  *physical_page_size = fsp_flags_get_zip_size(flags);
  *compressed = (*physical_page_size != 0);
  if (*physical_page_size == 0)
    *physical_page_size= *logical_page_size;

  return TRUE;
}

#ifdef __WIN__
/***********************************************//*
 @param		[in] error	error no. from the getLastError().

 @retval error message corresponding to error no.
*/
static
char*
win32_error_message(
	int	error)
{
	static char err_msg[1024] = {'\0'};
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)err_msg, sizeof(err_msg), NULL );

	return (err_msg);
}
#endif /* __WIN__ */

/***********************************************//*
 @param [in] name	name of file.
 @retval file pointer; file pointer is NULL when error occured.
*/

FILE*
open_file(
	const char*	name)
{
	int	fd;		/* file descriptor. */
	FILE*	fil_in;
#ifdef __WIN__
	HANDLE		hFile;		/* handle to open file. */
	DWORD		access;		/* define access control */
	int		flags;		/* define the mode for file
					descriptor */

	access = GENERIC_READ;
	flags = _O_RDONLY | _O_BINARY;
	hFile = CreateFile(
			(LPCTSTR) name, access, 0L, NULL,
			OPEN_EXISTING, NULL, NULL);

	if (hFile == INVALID_HANDLE_VALUE) {
		/* print the error message. */
		fprintf(stderr, "Filename::%s %s\n",
			win32_error_message(GetLastError()));

			return (NULL);
		}

	/* get the file descriptor. */
	fd= _open_osfhandle((intptr_t)hFile, flags);
#else /* __WIN__ */

	int	create_flag;
	create_flag = O_RDONLY;

	fd = open(name, create_flag);
	if ( -1 == fd) {
		perror("open");
		return (NULL);
	}

#endif /* __WIN__ */

	fil_in = fdopen(fd, "rb");

	return (fil_in);
}

/* command line argument to do page checks (that's it) */
/* another argument to specify page ranges... seek to right spot and go from there */

static struct my_option innochecksum_options[] =
{
  {"help", '?', "Displays this help and exits.",
    0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"info", 'I', "Synonym for --help.",
    0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"version", 'V', "Displays version information and exits.",
    0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"verbose", 'v', "Verbose (prints progress every 5 seconds).",
    &verbose, &verbose, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"debug", 'd', "Debug mode (prints checksums for each page, implies verbose).",
    &debug, &debug, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"debug", 'u', "Skip corrupt pages.",
    &skip_corrupt, &skip_corrupt, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"count", 'c', "Print the count of pages in the file.",
    &just_count, &just_count, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"start_page", 's', "Start on this page number (0 based).",
    &start_page, &start_page, 0, GET_ULL, REQUIRED_ARG,
    0, 0, ULONGLONG_MAX, 0, 1, 0},
  {"end_page", 'e', "End at this page number (0 based).",
    &end_page, &end_page, 0, GET_ULL, REQUIRED_ARG,
    0, 0, ULONGLONG_MAX, 0, 1, 0},
  {"page", 'p', "Check only this page (0 based).",
    &do_page, &do_page, 0, GET_ULL, REQUIRED_ARG,
    0, 0, ULONGLONG_MAX, 0, 1, 0},
  {"per_page_details", 'i', "Print out per-page detail information.",
    &per_page_details, &per_page_details, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0}
    ,
  {0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};

static void print_version(void)
{
  printf("%s Ver %s, for %s (%s)\n",
         my_progname, INNODB_VERSION_STR,
         SYSTEM_TYPE, MACHINE_TYPE);
}

static void usage(void)
{
  print_version();
  puts(ORACLE_WELCOME_COPYRIGHT_NOTICE("2000"));
  printf("InnoDB offline file checksum utility.\n");
  printf("Usage: %s [-c] [-s <start page>] [-e <end page>] [-p <page>] [-v] [-d] <filename>\n", my_progname);
  my_print_help(innochecksum_options);
  my_print_variables(innochecksum_options);
}

extern "C" my_bool
innochecksum_get_one_option(
/*========================*/
  int optid,
  const struct my_option *opt __attribute__((unused)),
  char *argument __attribute__((unused)))
{
  switch (optid) {
  case 'd':
    verbose=1;	/* debug implies verbose... */
    break;
  case 'e':
    use_end_page= 1;
    break;
  case 'p':
    end_page= start_page= do_page;
    use_end_page= 1;
    do_one_page= 1;
    break;
  case 'V':
    print_version();
    exit(0);
    break;
  case 'I':
  case '?':
    usage();
    exit(0);
    break;
  }
  return 0;
}

static int get_options(
/*===================*/
  int *argc,
  char ***argv)
{
  int ho_error;

  if ((ho_error=handle_options(argc, argv, innochecksum_options, innochecksum_get_one_option)))
    exit(ho_error);

  /* The next arg must be the filename */
  if (!*argc)
  {
    usage();
    return 1;
  }
  return 0;
} /* get_options */

/*********************************************************************//**
Gets the file page type.
@return type; NOTE that if the type has not been written to page, the
return value not defined */
ulint
fil_page_get_type(
/*==============*/
       uchar*  page)   /*!< in: file page */
{
       return(mach_read_from_2(page + FIL_PAGE_TYPE));
}

/**************************************************************//**
Gets the index id field of a page.
@return        index id */
ib_uint64_t
btr_page_get_index_id(
/*==================*/
       uchar*  page)   /*!< in: index page */
{
       return(mach_read_from_8(page + PAGE_HEADER + PAGE_INDEX_ID));
}

void
parse_page(
/*=======*/
       uchar* page) /* in: buffer page */
{
       ib_uint64_t id;
       ulint x;
       ulint n_recs;
       ulint page_no;
       ulint data_bytes;
       int is_leaf;
       int size_range_id;

       switch (fil_page_get_type(page)) {
       case FIL_PAGE_INDEX:
               n_fil_page_index++;
               id = btr_page_get_index_id(page);
               n_recs = page_get_n_recs(page);
               page_no = page_get_page_no(page);
               data_bytes = page_get_data_size(page);
               is_leaf = page_is_leaf(page);
               size_range_id = (data_bytes * SIZE_RANGES_FOR_PAGE
                                + logical_page_size - 1) /
                                logical_page_size;
               if (size_range_id > SIZE_RANGES_FOR_PAGE + 1) {
                 /* data_bytes is bigger than logical_page_size */
                 size_range_id = SIZE_RANGES_FOR_PAGE + 1;
               }
               if (per_page_details) {
                 printf("index %lu page %lu leaf %u n_recs %lu data_bytes %lu"
                         "\n", id, page_no, is_leaf, n_recs, data_bytes);
               }
               /* update per-index statistics */
               {
                 if (index_ids.count(id) == 0) {
                   index_ids.insert(std::make_pair(id, per_index_stats()));
                 }
                 per_index_stats &index = index_ids.find(id)->second;
                 index.pages++;
                 if (is_leaf) index.leaf_pages++;
                 index.total_n_recs += n_recs;
                 index.total_data_bytes += data_bytes;
                 index.pages_in_size_range[size_range_id] ++;
               }

               break;
       case FIL_PAGE_UNDO_LOG:
               if (per_page_details) {
                       printf("FIL_PAGE_UNDO_LOG\n");
               }
               n_fil_page_undo_log++;
               x = mach_read_from_2(page + TRX_UNDO_PAGE_HDR +
                                    TRX_UNDO_PAGE_TYPE);
               if (x == TRX_UNDO_INSERT)
                       n_undo_insert++;
               else if (x == TRX_UNDO_UPDATE)
                       n_undo_update++;
               else
                       n_undo_other++;

               x = mach_read_from_2(page + TRX_UNDO_SEG_HDR + TRX_UNDO_STATE);
               switch (x) {
                       case TRX_UNDO_ACTIVE: n_undo_state_active++; break;
                       case TRX_UNDO_CACHED: n_undo_state_cached++; break;
                       case TRX_UNDO_TO_FREE: n_undo_state_to_free++; break;
                       case TRX_UNDO_TO_PURGE: n_undo_state_to_purge++; break;
                       case TRX_UNDO_PREPARED: n_undo_state_prepared++; break;
                       default: n_undo_state_other++; break;
               }
               break;
       case FIL_PAGE_INODE:
               if (per_page_details) {
                       printf("FIL_PAGE_INODE\n");
               }
               n_fil_page_inode++;
               break;
       case FIL_PAGE_IBUF_FREE_LIST:
               if (per_page_details) {
                       printf("FIL_PAGE_IBUF_FREE_LIST\n");
               }
               n_fil_page_ibuf_free_list++;
               break;
       case FIL_PAGE_TYPE_ALLOCATED:
               if (per_page_details) {
                       printf("FIL_PAGE_TYPE_ALLOCATED\n");
               }
               n_fil_page_type_allocated++;
               break;
       case FIL_PAGE_IBUF_BITMAP:
               if (per_page_details) {
                       printf("FIL_PAGE_IBUF_BITMAP\n");
               }
               n_fil_page_ibuf_bitmap++;
               break;
       case FIL_PAGE_TYPE_SYS:
               if (per_page_details) {
                       printf("FIL_PAGE_TYPE_SYS\n");
               }
               n_fil_page_type_sys++;
               break;
       case FIL_PAGE_TYPE_TRX_SYS:
               if (per_page_details) {
                       printf("FIL_PAGE_TYPE_TRX_SYS\n");
               }
               n_fil_page_type_trx_sys++;
               break;
       case FIL_PAGE_TYPE_FSP_HDR:
               if (per_page_details) {
                       printf("FIL_PAGE_TYPE_FSP_HDR\n");
               }
               n_fil_page_type_fsp_hdr++;
               break;
       case FIL_PAGE_TYPE_XDES:
               if (per_page_details) {
                       printf("FIL_PAGE_TYPE_XDES\n");
               }
               n_fil_page_type_xdes++;
               break;
       case FIL_PAGE_TYPE_BLOB:
               if (per_page_details) {
                       printf("FIL_PAGE_TYPE_BLOB\n");
               }
               n_fil_page_type_blob++;
               break;
       case FIL_PAGE_TYPE_ZBLOB:
       case FIL_PAGE_TYPE_ZBLOB2:
               if (per_page_details) {
                       printf("FIL_PAGE_TYPE_ZBLOB/2\n");
               }
               n_fil_page_type_zblob++;
               break;
       default:
               if (per_page_details) {
                       printf("FIL_PAGE_TYPE_OTHER\n");
               }
               n_fil_page_type_other++;
       }
}

void
print_stats()
/*========*/
{
       unsigned long long i;

       printf("%d\tbad checksum\n", n_bad_checksum);
       printf("%d\tFIL_PAGE_INDEX\n", n_fil_page_index);
       printf("%d\tFIL_PAGE_UNDO_LOG\n", n_fil_page_undo_log);
       printf("%d\tFIL_PAGE_INODE\n", n_fil_page_inode);
       printf("%d\tFIL_PAGE_IBUF_FREE_LIST\n", n_fil_page_ibuf_free_list);
       printf("%d\tFIL_PAGE_TYPE_ALLOCATED\n", n_fil_page_type_allocated);
       printf("%d\tFIL_PAGE_IBUF_BITMAP\n", n_fil_page_ibuf_bitmap);
       printf("%d\tFIL_PAGE_TYPE_SYS\n", n_fil_page_type_sys);
       printf("%d\tFIL_PAGE_TYPE_TRX_SYS\n", n_fil_page_type_trx_sys);
       printf("%d\tFIL_PAGE_TYPE_FSP_HDR\n", n_fil_page_type_fsp_hdr);
       printf("%d\tFIL_PAGE_TYPE_XDES\n", n_fil_page_type_xdes);
       printf("%d\tFIL_PAGE_TYPE_BLOB\n", n_fil_page_type_blob);
       printf("%d\tFIL_PAGE_TYPE_ZBLOB\n", n_fil_page_type_zblob);
       printf("%d\tother\n", n_fil_page_type_other);
       printf("%d\tmax index_id\n", n_fil_page_max_index_id);
       printf("undo type: %d insert, %d update, %d other\n",
               n_undo_insert, n_undo_update, n_undo_other);
       printf("undo state: %d active, %d cached, %d to_free, %d to_purge,"
               " %d prepared, %d other\n", n_undo_state_active,
               n_undo_state_cached, n_undo_state_to_free,
               n_undo_state_to_purge, n_undo_state_prepared,
               n_undo_state_other);

       printf("index_id\t#pages\t\t#leaf_pages\t#recs_per_page"
               "\t#bytes_per_page\n");
       for (auto it = index_ids.begin(); it != index_ids.end(); it++) {
         const per_index_stats& index = it->second;
         printf("%lld\t\t%lld\t\t%lld\t\t%lld\t\t%lld\n",
                it->first, index.pages, index.leaf_pages,
                index.total_n_recs / index.pages,
                index.total_data_bytes / index.pages);
       }
       printf("\n");
       printf("index_id\tpage_data_bytes_histgram(empty,...,oversized)\n");
       for (auto it = index_ids.begin(); it != index_ids.end(); it++) {
         printf("%lld\t", it->first);
         const per_index_stats& index = it->second;
         for (i = 0; i < SIZE_RANGES_FOR_PAGE+2; i++) {
           printf("\t%lld", index.pages_in_size_range[i]);
         }
         printf("\n");
       }
}

int main(int argc, char **argv)
{
  FILE* f;                       /* our input file */
  char* filename;                /* our input filename. */
  unsigned char big_buf[UNIV_PAGE_SIZE_MAX*2]; /* Buffer to store pages read */
  unsigned char *buf = (unsigned char*)ut_align_down(big_buf
                       + UNIV_PAGE_SIZE_MAX, UNIV_PAGE_SIZE_MAX);
                                 /* Make sure the page is aligned */
  ulong bytes;                   /* bytes read count */
  ulint ct;                      /* current page number (0 based) */
  time_t now;                    /* current time */
  time_t lastt;                  /* last time */
  ulint oldcsum, oldcsumfield, csum, csumfield, logseq, logseqfield;
  dual_crc crc32s;               /* struct for the two forms of crc32c */

                                 /* ulints for checksum storage */
  /* stat, to get file size. */
#ifdef __WIN__
  struct _stat64 st;
#else
  struct stat st;
#endif
  unsigned long long int size;   /* size of file (has to be 64 bits) */
  ulint pages;                   /* number of pages in file */
  off_t offset= 0;
  bool compressed;

  printf("InnoDB offline file checksum utility.\n");

  ut_crc32_init();

  MY_INIT(argv[0]);

  if (get_options(&argc,&argv))
    exit(1);

  if (verbose)
    my_print_variables(innochecksum_options);

  /* The file name is not optional */
  filename = *argv;
  if (*filename == '\0')
  {
    fprintf(stderr, "Error; File name missing\n");
    return 1;
  }

  /* stat the file to get size and page count */
#ifdef __WIN__
  if (_stat64(filename, &st))
#else
  if (stat(filename, &st))
#endif
  {
    fprintf(stderr, "Error; %s cannot be found\n", filename);
    return 1;
  }
  size= st.st_size;

  /* Open the file for reading */
  f= open_file(filename);
  if (f == NULL) {
    return 1;
  }

  if (!get_page_size(f, buf, &logical_page_size, &physical_page_size,
                     &compressed))
  {
    return 1;
  }

  if (compressed)
  {
    printf("Table is compressed\n");
    printf("Key block size is %luK\n", physical_page_size);
  }
  else
  {
    printf("Table is uncompressed\n");
    printf("Page size is %luK\n", physical_page_size);
  }

  pages= (ulint) (size / physical_page_size);

  if (just_count)
  {
    if (verbose)
      printf("Number of pages: ");
    printf("%lu\n", pages);
    return 0;
  }
  else if (verbose)
  {
    printf("file %s = %llu bytes (%lu pages)...\n", filename, size, pages);
    if (do_one_page)
      printf("InnoChecksum; checking page %llu\n", do_page);
    else
      printf("InnoChecksum; checking pages in range %llu to %llu\n", start_page, use_end_page ? end_page : (pages - 1));
  }

#ifdef UNIV_LINUX
  if (posix_fadvise(fileno(f), 0, 0, POSIX_FADV_SEQUENTIAL) ||
      posix_fadvise(fileno(f), 0, 0, POSIX_FADV_NOREUSE))
  {
    perror("posix_fadvise failed");
  }
#endif

  /* seek to the necessary position */
  if (start_page)
  {

    offset= (off_t)start_page * (off_t)physical_page_size;

#ifdef __WIN__
	if (_fseeki64(f, offset, SEEK_SET)) {
#else
	if (fseeko(f, offset, SEEK_SET)) {
#endif /* __WIN__ */
	perror("Error; Unable to seek to necessary offset");
	return 1;
    }
  }

  /* main checksumming loop */
  ct= start_page;
  lastt= 0;
  while (!feof(f))
  {
    int page_ok = 1;

    bytes= ulong(fread(buf, 1, physical_page_size, f));
    if (!bytes && feof(f))
    {
      print_stats();
      return 0;
    }

    if (ferror(f))
    {
      fprintf(stderr, "Error reading %lu bytes", physical_page_size);
      perror(" ");
      return 1;
    }
    if (bytes != physical_page_size)
    {
      fprintf(stderr, "Error; bytes read (%lu) doesn't match page size (%lu)\n", bytes, physical_page_size);
      print_stats();
      return 1;
    }

    if (compressed) {
      /* compressed pages */
      if (!page_zip_verify_checksum(buf, physical_page_size)) {
        fprintf(stderr, "Fail; page %lu invalid (fails compressed page checksum).\n", ct);
        if (!skip_corrupt)
          return 1;
        page_ok = 0;
      }
    } else {
      /* check the "stored log sequence numbers" */
      logseq= mach_read_from_4(buf + FIL_PAGE_LSN + 4);
      logseqfield= mach_read_from_4(buf + logical_page_size - FIL_PAGE_END_LSN_OLD_CHKSUM + 4);
      if (debug)
        printf("page %lu: log sequence number: first = %lu; second = %lu\n", ct, logseq, logseqfield);
      if (logseq != logseqfield)
      {
        fprintf(stderr, "Fail; page %lu invalid (fails log sequence number check)\n", ct);
        if (!skip_corrupt) return 1;
        page_ok = 0;
      }
      /* check old method of checksumming */
      oldcsum= buf_calc_page_old_checksum(buf);
      oldcsumfield= mach_read_from_4(buf + logical_page_size - FIL_PAGE_END_LSN_OLD_CHKSUM);
      crc32s= buf_calc_page_crc32fb(buf);
      if (debug)
        printf("page %lu: old style: calculated = %lu; recorded = %lu\n", ct, oldcsum, oldcsumfield);
      if (oldcsumfield != mach_read_from_4(buf + FIL_PAGE_LSN)
          && oldcsumfield != oldcsum && crc32s.crc32c != oldcsumfield
          && crc32s.crc32cfb != oldcsumfield)
      {
        fprintf(stderr, "Fail;  page %lu invalid (fails old style checksum)\n", ct);
        if (!skip_corrupt) return 1;
        page_ok = 0;
      }
      /* now check the new method */
      csum= buf_calc_page_new_checksum(buf);
      csumfield= mach_read_from_4(buf + FIL_PAGE_SPACE_OR_CHKSUM);
      if (debug)
        printf("page %lu: new style: calculated = %lu; crc32c = %u; crc32cfb = %u; recorded = %lu\n",
               ct, csum, crc32s.crc32c, crc32s.crc32cfb, csumfield);
      if (csumfield != 0 && crc32s.crc32c != csumfield && crc32s.crc32cfb != csumfield && csum != csumfield)
      {
        fprintf(stderr, "Fail; page %lu invalid (fails innodb and crc32 checksum)\n", ct);
        if (!skip_corrupt) return 1;
        page_ok = 0;
      }
    }

    /* end if this was the last page we were supposed to check */
    if (use_end_page && (ct >= end_page))
    {
      print_stats();
      return 0;
    }

    if (per_page_details)
    {
      printf("page %ld ", ct);
    }

    ct++;

    if (!page_ok)
    {
      if (per_page_details)
      {
        printf("BAD_CHECKSUM\n");
      }
      n_bad_checksum++;
      continue;
    }

    parse_page(buf);

    /* progress printing */
    if (verbose)
    {
      if (ct % 10000 == 0)
      {
        now= time(0);
        if (!lastt) lastt= now;
        if (now - lastt >= 1)
        {
          fprintf(stderr, "page %lu okay: %.3f%% done\n", (ct - 1), (float) ct / pages * 100);
          lastt= now;
        }
      }
    }
  }
  print_stats();

  return 0;
}

