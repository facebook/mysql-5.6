# Copyright (C) 2008 Sun Microsystems, Inc. All rights reserved.  Use
# is subject to license terms.
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

query_init:
        { $global->set("count",0) } SHOW TABLES ;
query: 
        { $global->set("count",$global->get("count")+1); $stack->push(); $stack->set("arg","LEFT"); } SELECT * from join { $stack->pop(undef) } |
        { $global->set("count",$global->get("count")+1); $stack->push(); $stack->set("arg","RIGHT"); } SELECT * from join { $stack->pop(undef) } ;

join:
       { $stack->push() }      
       table_or_join 
       { $stack->set("left",$stack->get("result")); }
       { $stack->get("arg") } JOIN 
       table_or_join 
       ON 
       { $prng->arrayElement($stack->get("left")).".col = ".$prng->arrayElement($stack->get("result")).".col" }
       { $stack->pop([keys %{{map {$_=>1} (@{$stack->get("left")},@{$stack->get("result")})}}]) } ;

table_or_join:
        table | table | join ;

table:
       { $stack->push(); my $x = "s".$global->get("count").".t".$prng->digit();  $stack->pop([$x]); $x } ;
