/*****************************************************************************

Copyright (c) 2006, 2011, Oracle and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

/**************************************************//**
@file buf/buf0buddy.cc
Binary buddy allocator for compressed pages

Created December 2006 by Marko Makela
*******************************************************/

#define THIS_MODULE
#include "buf0buddy.h"
#ifdef UNIV_NONINL
# include "buf0buddy.ic"
#endif
#undef THIS_MODULE
#include "buf0buf.h"
#include "buf0lru.h"
#include "buf0flu.h"
#include "page0zip.h"

/* In order to avoid malloc on insert into the zip_free trees we use the frames
themselves as the node storage. This structure defines the value element of
ib_rbt_node_t nodes in the zip_free trees.

Previously, the code cast the frame pointer to a buf_page_t and used
state == BUF_BLOCK_ZIP_FREE as a simple verification mechanism. However,
that verification was of limited usefulness because BUF_BLOCK_ZIP_FREE == 0,
which is a likely value to be in that memory location even when the frame
is in use. Therefore, use a magic_num instead, which is less likely to match
a value should the frame be in use. */
typedef	struct zip_free_value_struct	zip_free_value_t;
struct zip_free_value_struct{
	ulint	magic_n;
	byte*	frame;
};

const ulint	ZIP_FREE_MAGIC_N = 957314685;

/**********************************************************************//**
Compare 2 keys for the zip_free trees. */
static int buf_buddy_zip_free_cmp(const void* k1, const void* k2)
{
	zip_free_value_t* v1 = (zip_free_value_t*)k1;
	zip_free_value_t* v2 = (zip_free_value_t*)k2;

	ut_ad(v1->magic_n == ZIP_FREE_MAGIC_N);
	ut_ad(v2->magic_n == ZIP_FREE_MAGIC_N);

	if (v1->frame < v2->frame)
		return -1;
	else if (v1->frame > v2->frame)
		return 1;
	return 0;
}

/**********************************************************************//**
Initialize the buddy allocator.
@return	TRUE on success, FALSE on failure */
UNIV_INTERN
ibool
buf_buddy_init(
	buf_pool_t*	buf_pool)	/*!< in: buffer pool being init'ed */
{
	uint i;
	for (i = 0; i < BUF_BUDDY_SIZES_MAX; i++) {
		/* Not checking return of rbt_create because, if the allocs
		failed, then rbt_create already SEGV'ed dereferencing the
		NULL pointers. */
		buf_pool->zip_free[i] = rbt_create(sizeof(zip_free_value_t),
						   buf_buddy_zip_free_cmp);
	}
	return TRUE;
}

/**********************************************************************//**
Frees the buddy allocator at shutdown. */
UNIV_INTERN
void
buf_buddy_shutdown(
	buf_pool_t*	buf_pool)	/*!< in: buffer pool being shutdown */
{
	uint i;
	for (i = 0; i < BUF_BUDDY_SIZES_MAX; i++) {
		if (buf_pool->zip_free[i]) {
			/* Any nodes in the tree are really just pointers to
			frames in the buffer pool. Thus, remove them from the
			tree so that the tree doesn't try to free them. */
			const ib_rbt_node_t* node;
			while ((node = rbt_first(buf_pool->zip_free[i])))
				rbt_remove_node(buf_pool->zip_free[i], node);
			rbt_free(buf_pool->zip_free[i]);
		}
	}
}

/**********************************************************************//**
Get the offset of the buddy of a compressed page frame.
@return	the buddy relative of page */
UNIV_INLINE
byte*
buf_buddy_get(
/*==========*/
	byte*	page,	/*!< in: compressed page */
	ulint	size)	/*!< in: page size in bytes */
{
	ut_ad(ut_is_2pow(size));
	ut_ad(size >= BUF_BUDDY_LOW);
	ut_ad(BUF_BUDDY_LOW <= UNIV_ZIP_SIZE_MIN);
	ut_ad(size < BUF_BUDDY_HIGH);
	ut_ad(BUF_BUDDY_HIGH == UNIV_PAGE_SIZE);
	ut_ad(!ut_align_offset(page, size));

	if (((ulint) page) & size) {
		return(page - size);
	} else {
		return(page + size);
	}
}

/***********************************************************************//**
Validate a given zip_free tree. */
UNIV_INLINE
void
buf_buddy_zip_free_validate(
	buf_pool_t*	buf_pool,	/*!< in: buffer pool instance */
	ulint		i)		/*!< in: index of
					buf_pool->zip_free[] */
{
	const ib_rbt_node_t* node;
	ut_ad(rbt_validate(buf_pool->zip_free[i]));
	for (node = rbt_first(buf_pool->zip_free[i]);
	     node != NULL;
	     node = rbt_next(buf_pool->zip_free[i], node)) {
		ut_a(rbt_value(zip_free_value_t, node)->magic_n
		     == ZIP_FREE_MAGIC_N);
	}
}

/**********************************************************************//**
Add a block to the head of the appropriate buddy free list. */
UNIV_INLINE
void
buf_buddy_add_to_free(
/*==================*/
	buf_pool_t*	buf_pool,	/*!< in: buffer pool instance */
	byte*		frame,		/*!< in,own: frame to be freed */
	ulint		i)		/*!< in: index of
					buf_pool->zip_free[] */
{
	zip_free_value_t	key;
	const ib_rbt_node_t*	node;
	zip_free_value_t*	value;

	ut_ad(buf_pool_mutex_own(buf_pool));

	key.magic_n = ZIP_FREE_MAGIC_N;
	key.frame = frame;

	ut_ad(rbt_lookup(buf_pool->zip_free[i], &key) == NULL);

	node = rbt_insert_use_mem(buf_pool->zip_free[i], &key, (void*)frame);

	value = rbt_value(zip_free_value_t, node);
	value->magic_n = ZIP_FREE_MAGIC_N;
	value->frame = frame;
}

/**********************************************************************//**
Remove a block from the appropriate buddy free list. */
UNIV_INLINE
void
buf_buddy_remove_from_free(
/*=======================*/
	buf_pool_t*		buf_pool,	/*!< in: buffer pool instance */
	const ib_rbt_node_t*	node,		/*!< in: node to be removed */
	ulint			i)		/*!< in: index of
						buf_pool->zip_free[] */
{
#ifdef UNIV_DEBUG
	const ib_rbt_node_t*	prev = rbt_prev(buf_pool->zip_free[i], node);
	const ib_rbt_node_t*	next = rbt_next(buf_pool->zip_free[i], node);

	ut_ad(!prev ||
	      rbt_value(zip_free_value_t, prev)->magic_n == ZIP_FREE_MAGIC_N);
	ut_ad(!next ||
	      rbt_value(zip_free_value_t, next)->magic_n == ZIP_FREE_MAGIC_N);
#endif /* UNIV_DEBUG */

	ut_ad(buf_pool_mutex_own(buf_pool));
	ut_ad(rbt_value(zip_free_value_t, node)->magic_n == ZIP_FREE_MAGIC_N);
	ut_ad(rbt_value(zip_free_value_t, node)->frame == (byte*)node);
	rbt_remove_node(buf_pool->zip_free[i], node);
}

/**********************************************************************//**
Try to allocate a block from buf_pool->zip_free[].
@return	allocated block, or NULL if buf_pool->zip_free[] was empty */
static
byte*
buf_buddy_alloc_zip(
/*================*/
	buf_pool_t*	buf_pool,	/*!< in: buffer pool instance */
	ulint		i)		/*!< in: index of buf_pool->zip_free[] */
{
	byte*	frame = NULL;

	ut_ad(buf_pool_mutex_own(buf_pool));
	ut_a(i < BUF_BUDDY_SIZES);
	ut_a(i >= buf_buddy_get_slot(UNIV_ZIP_SIZE_MIN));

	ut_d(buf_buddy_zip_free_validate(buf_pool, i));

	if (rbt_size(buf_pool->zip_free[i])) {
		const ib_rbt_node_t*	node;
		node = rbt_first(buf_pool->zip_free[i]);
		ut_ad(rbt_value(zip_free_value_t, node)->magic_n
		      == ZIP_FREE_MAGIC_N);

		frame = rbt_value(zip_free_value_t, node)->frame;
		buf_buddy_remove_from_free(buf_pool, node, i);
	} else if (i + 1 < BUF_BUDDY_SIZES) {
		/* Attempt to split. */
		frame = buf_buddy_alloc_zip(buf_pool, i + 1);

		if (frame) {
			byte*	buddy = frame + (BUF_BUDDY_LOW << i);

			ut_ad(!buf_pool_contains_zip(buf_pool, buddy));
			ut_d(memset(buddy, i, BUF_BUDDY_LOW << i));
			buf_buddy_add_to_free(buf_pool, buddy, i);
		}
	}

	if (frame) {
		ut_d(memset(frame, ~i, BUF_BUDDY_LOW << i));
		UNIV_MEM_ALLOC(frame, BUF_BUDDY_SIZES << i);
	}

	return(frame);
}

/**********************************************************************//**
Deallocate a buffer frame of UNIV_PAGE_SIZE. */
static
void
buf_buddy_block_free(
/*=================*/
	buf_pool_t*	buf_pool,	/*!< in: buffer pool instance */
	void*		buf)		/*!< in: buffer frame to deallocate */
{
	const ulint	fold	= BUF_POOL_ZIP_FOLD_PTR(buf);
	buf_page_t*	bpage;
	buf_block_t*	block;

	ut_ad(buf_pool_mutex_own(buf_pool));
	ut_ad(!mutex_own(&buf_pool->zip_mutex));
	ut_a(!ut_align_offset(buf, UNIV_PAGE_SIZE));

	HASH_SEARCH(hash, buf_pool->zip_hash, fold, buf_page_t*, bpage,
		    ut_ad(buf_page_get_state(bpage) == BUF_BLOCK_MEMORY
			  && bpage->in_zip_hash && !bpage->in_page_hash),
		    ((buf_block_t*) bpage)->frame == buf);
	ut_a(bpage);
	ut_a(buf_page_get_state(bpage) == BUF_BLOCK_MEMORY);
	ut_ad(!bpage->in_page_hash);
	ut_ad(bpage->in_zip_hash);
	ut_d(bpage->in_zip_hash = FALSE);
	HASH_DELETE(buf_page_t, hash, buf_pool->zip_hash, fold, bpage);

	ut_d(memset(buf, 0, UNIV_PAGE_SIZE));
	UNIV_MEM_INVALID(buf, UNIV_PAGE_SIZE);

	block = (buf_block_t*) bpage;
	mutex_enter(&block->mutex);
	buf_LRU_block_free_non_file_page(block);
	mutex_exit(&block->mutex);

	ut_ad(buf_pool->buddy_n_frames > 0);
	ut_d(buf_pool->buddy_n_frames--);
}

/**********************************************************************//**
Allocate a buffer block to the buddy allocator. */
static
void
buf_buddy_block_register(
/*=====================*/
	buf_block_t*	block)	/*!< in: buffer frame to allocate */
{
	buf_pool_t*	buf_pool = buf_pool_from_block(block);
	const ulint	fold = BUF_POOL_ZIP_FOLD(block);
	ut_ad(buf_pool_mutex_own(buf_pool));
	ut_ad(!mutex_own(&buf_pool->zip_mutex));
	ut_ad(buf_block_get_state(block) == BUF_BLOCK_READY_FOR_USE);

	buf_block_set_state(block, BUF_BLOCK_MEMORY);

	ut_a(block->frame);
	ut_a(!ut_align_offset(block->frame, UNIV_PAGE_SIZE));

	ut_ad(!block->page.in_page_hash);
	ut_ad(!block->page.in_zip_hash);
	ut_d(block->page.in_zip_hash = TRUE);
	HASH_INSERT(buf_page_t, hash, buf_pool->zip_hash, fold, &block->page);

	ut_d(buf_pool->buddy_n_frames++);
}

/**********************************************************************//**
Allocate a block from a bigger object.
@return	allocated block */
static
void*
buf_buddy_alloc_from(
/*=================*/
	buf_pool_t*	buf_pool,	/*!< in: buffer pool instance */
	byte*		buf,		/*!< in: a block that is free to use */
	ulint		i,		/*!< in: index of
					buf_pool->zip_free[] */
	ulint		j)		/*!< in: size of buf as an index
					of buf_pool->zip_free[] */
{
	ulint	offs	= BUF_BUDDY_LOW << j;
	ut_ad(j <= BUF_BUDDY_SIZES);
	ut_ad(i >= buf_buddy_get_slot(UNIV_ZIP_SIZE_MIN));
	ut_ad(j >= i);
	ut_ad(!ut_align_offset(buf, offs));

	/* Add the unused parts of the block to the free lists. */
	while (j > i) {
		byte*	frame;

		offs >>= 1;
		j--;

		frame = buf + offs;
		ut_d(memset(frame, j, BUF_BUDDY_LOW << j));
		ut_d(buf_buddy_zip_free_validate(buf_pool, i));
		buf_buddy_add_to_free(buf_pool, frame, j);
	}

	return(buf);
}

/**********************************************************************//**
Allocate a block.  The thread calling this function must hold
buf_pool->mutex and must not hold buf_pool->zip_mutex or any block->mutex.
The buf_pool_mutex may be released and reacquired.
@return	allocated block, never NULL */
UNIV_INTERN
void*
buf_buddy_alloc_low(
/*================*/
	buf_pool_t*	buf_pool,	/*!< in/out: buffer pool instance */
	ulint		i,		/*!< in: index of buf_pool->zip_free[],
					or BUF_BUDDY_SIZES */
	ibool*		lru)		/*!< in: pointer to a variable that
					will be assigned TRUE if storage was
					allocated from the LRU list and
					buf_pool->mutex was temporarily
					released */
{
	buf_block_t*	block;

	ut_ad(lru);
	ut_ad(buf_pool_mutex_own(buf_pool));
	ut_ad(!mutex_own(&buf_pool->zip_mutex));
	ut_ad(i >= buf_buddy_get_slot(UNIV_ZIP_SIZE_MIN));

	if (i < BUF_BUDDY_SIZES) {
		/* Try to allocate from the buddy system. */
		block = (buf_block_t*) buf_buddy_alloc_zip(buf_pool, i);

		if (block) {
			goto func_exit;
		}
	}

	/* Try allocating from the buf_pool->free list. */
	block = buf_LRU_get_free_only(buf_pool);

	if (block) {

		goto alloc_big;
	}

	/* Try replacing an uncompressed page in the buffer pool. */
	buf_pool_mutex_exit(buf_pool);
	block = buf_LRU_get_free_block(buf_pool);
	*lru = TRUE;
	buf_pool_mutex_enter(buf_pool);

alloc_big:
	buf_buddy_block_register(block);

	block = (buf_block_t*) buf_buddy_alloc_from(
		buf_pool, block->frame, i, BUF_BUDDY_SIZES);

func_exit:
	buf_pool->buddy_stat[i].used++;
	return(block);
}

/**********************************************************************//**
Try to relocate a block.
@return	TRUE if relocated */
static
ibool
buf_buddy_relocate(
/*===============*/
	buf_pool_t*	buf_pool,	/*!< in: buffer pool instance */
	void*		src,		/*!< in: block to relocate */
	void*		dst,		/*!< in: free block to relocate to */
	ulint		i)		/*!< in: index of
					buf_pool->zip_free[] */
{
	buf_page_t*	bpage;
	const ulint	size	= BUF_BUDDY_LOW << i;
	ib_mutex_t*	mutex;
	ulint		space;
	ulint		page_no;

	ut_ad(buf_pool_mutex_own(buf_pool));
	ut_ad(!mutex_own(&buf_pool->zip_mutex));
	ut_ad(!ut_align_offset(src, size));
	ut_ad(!ut_align_offset(dst, size));
	ut_ad(i >= buf_buddy_get_slot(UNIV_ZIP_SIZE_MIN));
	UNIV_MEM_ASSERT_W(dst, size);

	/* We assume that all memory from buf_buddy_alloc()
	is used for compressed page frames. */

	/* We look inside the allocated objects returned by
	buf_buddy_alloc() and assume that each block is a compressed
	page that contains a valid space_id and page_no in the page
	header. Should the fields be invalid, we will be unable to
	relocate the block. */

	/* The src block may be split into smaller blocks,
	some of which may be free.  Thus, the
	mach_read_from_4() calls below may attempt to read
	from free memory.  The memory is "owned" by the buddy
	allocator (and it has been allocated from the buffer
	pool), so there is nothing wrong about this.  The
	mach_read_from_4() calls here will only trigger bogus
	Valgrind memcheck warnings in UNIV_DEBUG_VALGRIND builds. */
	space	= mach_read_from_4((const byte*) src
				   + FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID);
	page_no	= mach_read_from_4((const byte*) src
				   + FIL_PAGE_OFFSET);
	/* Suppress Valgrind warnings about conditional jump
	on uninitialized value. */
	UNIV_MEM_VALID(&space, sizeof space);
	UNIV_MEM_VALID(&page_no, sizeof page_no);
	bpage = buf_page_hash_get(buf_pool, space, page_no);

	if (!bpage || bpage->zip.data != src) {
		/* The block has probably been freshly
		allocated by buf_LRU_get_free_block() but not
		added to buf_pool->page_hash yet.  Obviously,
		it cannot be relocated. */

		return(FALSE);
	}

	if (page_zip_get_size(&bpage->zip) != size) {
		/* The block is of different size.  We would
		have to relocate all blocks covered by src.
		For the sake of simplicity, give up. */
		ut_ad(page_zip_get_size(&bpage->zip) < size);

		return(FALSE);
	}

	/* The block must have been allocated, but it may
	contain uninitialized data. */
	UNIV_MEM_ASSERT_W(src, size);

	mutex = buf_page_get_mutex(bpage);

	mutex_enter(mutex);

	if (buf_page_can_relocate(bpage)) {
		/* Relocate the compressed page. */
		ullint	usec	= ut_time_us(NULL);
		ut_a(bpage->zip.data == src);
		memcpy(dst, src, size);
		bpage->zip.data = (page_zip_t*) dst;
		mutex_exit(mutex);
		UNIV_MEM_INVALID(src, size);
		{
			buf_buddy_stat_t*	buddy_stat
				= &buf_pool->buddy_stat[i];
			buddy_stat->relocated++;
			buddy_stat->relocated_usec
				+= ut_time_us(NULL) - usec;
		}
		return(TRUE);
	}

	mutex_exit(mutex);
	return(FALSE);
}

/**********************************************************************//**
Deallocate a block. */
UNIV_INTERN
void
buf_buddy_free_low(
/*===============*/
	buf_pool_t*	buf_pool,	/*!< in: buffer pool instance */
	void*		buf,		/*!< in: block to be freed, must not be
					pointed to by the buffer pool */
	ulint		i)		/*!< in: index of buf_pool->zip_free[],
					or BUF_BUDDY_SIZES */
{
	const ib_rbt_node_t*	node;
	byte*			frame;
	byte*			buddy;
	zip_free_value_t	key;

	ut_ad(buf_pool_mutex_own(buf_pool));
	ut_ad(!mutex_own(&buf_pool->zip_mutex));
	ut_ad(i <= BUF_BUDDY_SIZES);
	ut_ad(i >= buf_buddy_get_slot(UNIV_ZIP_SIZE_MIN));
	ut_ad(buf_pool->buddy_stat[i].used > 0);

	buf_pool->buddy_stat[i].used--;
recombine:
	UNIV_MEM_ASSERT_AND_ALLOC(buf, BUF_BUDDY_LOW << i);

	if (i == BUF_BUDDY_SIZES) {
		buf_buddy_block_free(buf_pool, buf);
		return;
	}

	ut_ad(i < BUF_BUDDY_SIZES);
	ut_ad(buf == ut_align_down(buf, BUF_BUDDY_LOW << i));
	ut_ad(!buf_pool_contains_zip(buf_pool, buf));

	/* Do not recombine blocks if there are few free blocks.
	We may waste up to 15360*max_len bytes to free blocks
	(1024 + 2048 + 4096 + 8192 = 15360) */
	if (rbt_size(buf_pool->zip_free[i]) < 16) {
		goto func_exit;
	}

	/* Try to combine adjacent blocks. */
	buddy = buf_buddy_get(((byte*) buf), BUF_BUDDY_LOW << i);

#ifndef UNIV_DEBUG_VALGRIND
	/* When Valgrind instrumentation is not enabled, we can read
	buddy->state to quickly determine that a block is not free.
	When the block is not free, buddy->state belongs to a compressed
	page frame that may be flagged uninitialized in our Valgrind
	instrumentation.  */

	if (rbt_value(zip_free_value_t, ((ib_rbt_node_t*)buddy))->magic_n
	    != ZIP_FREE_MAGIC_N) {

		goto buddy_nonfree;
	}
#endif /* !UNIV_DEBUG_VALGRIND */

	key.magic_n = ZIP_FREE_MAGIC_N;
	key.frame = buddy;
	node = rbt_lookup(buf_pool->zip_free[i], &key);
	if (node != NULL) {
		/* The buddy is free: recombine */
		buf_buddy_remove_from_free(buf_pool, node, i);
buddy_is_free:
		ut_ad(rbt_value(zip_free_value_t,
				((ib_rbt_node_t*)buddy))->magic_n
		      == ZIP_FREE_MAGIC_N);
		ut_ad(!buf_pool_contains_zip(buf_pool, buddy));
		i++;
		buf = ut_align_down(buf, BUF_BUDDY_LOW << i);

		goto recombine;
	}

#ifndef UNIV_DEBUG_VALGRIND
buddy_nonfree:
#endif /* !UNIV_DEBUG_VALGRIND */

	ut_d(buf_buddy_zip_free_validate(buf_pool, i));

	/* The buddy is not free. Is there a free block of this size? */
	if (rbt_size(buf_pool->zip_free[i])) {

		/* Remove the block from the free list, because a successful
		buf_buddy_relocate() will overwrite the frame. */
		node = rbt_first(buf_pool->zip_free[i]);
		buf_buddy_remove_from_free(buf_pool, node, i);

		/* Try to relocate the buddy of buf to the free block. */
		if (buf_buddy_relocate(buf_pool, buddy,
				       rbt_value(zip_free_value_t,
						 node)->frame,
				       i)) {

			rbt_value(zip_free_value_t,
				  ((ib_rbt_node_t*)buddy))->magic_n =
			  ZIP_FREE_MAGIC_N;
			goto buddy_is_free;
		}

		buf_buddy_add_to_free(buf_pool, (byte*)node, i);
	}

func_exit:
	/* Free the block to the buddy list. */
	frame = (byte*) buf;

	/* Fill large blocks with a constant pattern. */
	ut_d(memset(frame, i, BUF_BUDDY_LOW << i));
	UNIV_MEM_INVALID(frame, BUF_BUDDY_LOW << i);

	buf_buddy_add_to_free(buf_pool, frame, i);
}
