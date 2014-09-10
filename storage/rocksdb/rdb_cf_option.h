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

#include <map>

/*
  Numeric per-column family option value.

  Per-column family option can be set
  - Globally (the same value applies to all column families)
  - Per column family: there is a {cf_name -> value} map,
    and also there is a default value which applies to column
    families not found in the map.
*/
class Numeric_cf_option
{
public:
  typedef std::map<std::string, longlong> NameToLonglong;

  /* cf_name -> value map*/
  NameToLonglong name_map;

  /* The default value (if there is only one value, it is stored here) */
  longlong default_val;

  /* Get option value for column family with name cf_name */
  longlong get_val(const char *cf_name)
  {
    NameToLonglong::iterator it= name_map.find(cf_name);
    if (it != name_map.end())
    {
      return it->second;
    }
    else
      return default_val;
  }

  longlong get_default_val() { return default_val; }
};

/*
  Parse string representation of per-column family option and store it in *out
*/
bool parse_per_cf_numeric(const char *str, Numeric_cf_option *out);
