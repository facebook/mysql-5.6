/*****************************************************************************

Copyright (c) 1996, 2009, Oracle and/or its affiliates. All Rights Reserved.

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
@file include/lock0types.h
The transaction lock system global types

Created 5/7/1996 Heikki Tuuri
*******************************************************/

#ifndef lock0types_h
#define lock0types_h

#define lock_t ib_lock_t
struct lock_t;
struct lock_sys_t;

/* Basic lock modes */
enum lock_mode {
	LOCK_IS = 0,	/* intention shared */
	LOCK_IX,	/* intention exclusive */
	LOCK_S,		/* shared */
	LOCK_X,		/* exclusive */
	LOCK_AUTO_INC,	/* locks the auto-inc counter of a table
			in an exclusive mode */
	LOCK_NONE,	/* this is used elsewhere to note consistent read */
	LOCK_NUM = LOCK_NONE, /* number of lock modes */
	LOCK_NONE_UNSET = 255
};

/* X-lock modes : these are complements to LOCK_X
   LOCK_X + LOCK_X_SKIP_LOCKED is for SELECT ... FOR UPDATE SKIP LOCKED.
   LOCK_X + LOCK_X_NOWAIT is for SELECT ... FOR UPDATE NOWAIT.
   LOCK_X_REGULAR is the default value, which means the LOCK_X is regular.
   It will be ignored if X-lock modes combine with other lock modes.

   There are a couple of lock modes in InnoDB but basically there are two
   categories: shared lock (LOCK_S) and exclusive lock (LOCK_X) (let's
   ignore other lock modes for now). These lock modes define both the
   locks *on* a row and the behavior when *adding* a lock on a row that
   has been locked, i.e. when a lock is added on a row, this lock can be
   LOCK_S or LOCK_X , and when a transaction is *adding* a LOCK_S /
   LOCK_X on a row that has been locked by LOCK_S / LOCK_X (by other
   transactions), it either can lock it or get blocked on it. But SKIP
   LOCKED / NOWAIT are *not* new lock modes, i.e, a lock added on a row
   is still either LOCK_S or LOCK_X, the new sub lock mode
   LOCK_X_SKIP_LOCKED / LOCK_X_NOWAIT only defines the *behavior* that
   when a transaction is trying to put a LOCK_X on a row has been locked
   with a LOCK_X already (by another transaction), it won't be blocked,
   instead it will skip it to find the next match if the sub mode is
   LOCK_X_SKIP_LOCKED, or return with failure directly if the sub mode
   is LOCK_X_NOWAIT. So in a nutshell, these new sub modes
   LOCK_X_SKIP_LOCKED / LOCK_X_NOWAIT introduce new non-blocking
   behavior when locking conflict happens, they are *not* new lock modes.
*/
enum x_lock_mode {
	LOCK_X_REGULAR = 0, /* regular */
	LOCK_X_SKIP_LOCKED, /* skip locked */
	LOCK_X_NOWAIT, /* nowait */
	LOCK_X_MODE_COUNT
};

#endif
