/*
   Copyright (c) 2016, Facebook, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/* This C++ file's header */
#include "./rdb_utils.h"

/* C++ standard header files */
#include <ctype.h>

namespace myrocks {

/*
  Scan through an input string looking for pattern, ignoring case
  and skipping all data enclosed in quotes.
*/
const char* rdb_find_in_string(const char *str, const char *pattern)
{
  char quote = '\0';
  bool escape = false;
  bool found = false;

  for ( ; *str && !found; str++)
  {
    /* If we found a our starting quote character */
    if (*str == quote)
    {
      /* If it was escaped ignore it */
      if (escape)
      {
        escape = false;
      }
      /* Otherwise we are now outside of the quoted string */
      else
      {
        quote = '\0';
      }
    }
    /* Else if we are currently inside a quoted string? */
    else if (quote != '\0')
    {
      /* If so, check for the escape character */
      escape = (*str == '\\');
    }
    /* Else if we found a quote we are starting a quoted string */
    else if (*str == '"' || *str == '\'' || *str == '`')
    {
      quote = *str;
    }
    /* Else we are outside of a quoted string - look for our pattern */
    else
    {
      found = true;
      for (size_t ii = 0; pattern[ii]; ii++)
      {
        if (toupper(static_cast<int>(str[ii])) !=
            toupper(static_cast<int>(pattern[ii])))
        {
          found = false;
          break;
        }
      }
    }
  }

  return found ? str : nullptr;
}

}  // namespace myrocks
