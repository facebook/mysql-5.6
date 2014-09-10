/*
   Copyright (c) 2014, SkySQL Ab

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

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation        // gcc: Class implementation
#endif

#include <mysql/plugin.h>
#include "ha_rocksdb.h"

#include "rdb_cf_option.h"


/*
  Read a number from a string. The number may have a suffix Kk/Mm/Gg.
  The code is copied from eval_num_suffix().

  @param  str     INOUT string pointer
  @param  number  OUT   number

  @return 0 - OK
          1 - Error
*/

bool read_number_and_suffix(const char **str, longlong *number)
{
  char *endchar;
  longlong num;

  errno= 0;
  num= strtoll((char*)*str, &endchar, 10);
  if (errno == ERANGE)
    return true;

  int suffix_chars= 1;
  if (*endchar == 'k' || *endchar == 'K')
    num*= 1024L;
  else if (*endchar == 'm' || *endchar == 'M')
    num*= 1024L * 1024L;
  else if (*endchar == 'g' || *endchar == 'G')
    num*= 1024L * 1024L * 1024L;
  else
    suffix_chars= 0;

  endchar += suffix_chars;
  *str= endchar;
  *number= num;
  return 0;
}


/*
  Read column family name, followed by semicolon ':'

  @param  str     INOUT string pointer. On return points to right after
                        the semicolon.
  @return 0 - OK
          1 - Error
*/

bool read_cf_name(const char **str_arg)
{
  const char *str= *str_arg;
  while (str[0] && str[0] !=':') str++;

  if (str[0] == ':')
  {
    *str_arg= str + 1;
    return 0; /* OK */
  }
  return 1; /* Error */
}


/*
  Parse a string value that maybe either
  - a single number-with-suffix
  - a line in form
     cfname:value,cfname2:value2,...

  @param  str        String with option value
  @param  out  OUT   Parsed option value

  @return false  OK
  @return true   Parse error
*/

bool parse_per_cf_numeric(const char *str, Numeric_cf_option *out)
{
  longlong num;
  const char *p= str;
  if (!read_number_and_suffix(&p, &num) && p[0]==0)
  {
    /* The whole string is one number. Assign it as default and exit */
    out->default_val= num;
    return false;
  }

  /* Try reading it as a comma-separated list of cfname:value pairs*/
  while (1)
  {
    const char *cf_name= str;
    if (read_cf_name(&str))
      return true;
    /* -1 is to omit the ':' from cf name string: */
    std::string cf_name_str(cf_name, str - cf_name - 1);
    if (read_number_and_suffix(&str, &num))
      return true;

    out->name_map[cf_name_str]= num;

    /*
      However, if there is column family named "DEFAULT" it is take in as
      default.
    */
    if (!strcmp(cf_name_str.c_str(), DEFAULT_CF_NAME))
      out->default_val= num;

    /* Skip the comma */
    if (*str == ',')
      str++;
    else if (*str == 0)
      break;
    else
      return true;
  }
  return false;
}
