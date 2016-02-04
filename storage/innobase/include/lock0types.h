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
*/
enum x_lock_mode {
	LOCK_X_REGULAR = 0, /* regular */
	LOCK_X_SKIP_LOCKED, /* skip locked */
	LOCK_X_NOWAIT, /* nowait */
	LOCK_X_MODE_COUNT
};

#endif
