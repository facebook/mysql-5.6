# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved. 
# Use is subject to license terms.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
# USA


# This is a grammar for testing --sqltrace functionality.
# --sqltrace may behave differently depending on its value, or whether or not
# it has a value at all.
#
# We make sure that runs of this grammar will produce at least one valid and
# one invalid query.

query:
    valid; invalid;
    
valid:
    SELECT pk, col_int_key FROM _table |
    SELECT pk, col_int_key FROM _table WHERE pk > _int ;
    
invalid:
    SELECT pk, non-existent-column FROM _table;

