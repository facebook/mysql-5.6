/*
   Copyright (c) 2016, Facebook. All rights reserved.

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


/* This file defines all lua string functions */


/* May include caustic 3rd-party defs. Use early, so it can override nothing. */
#include "my_global.h"

/*
  It is necessary to include set_var.h instead of item.h because there
  are dependencies on include order for set_var.h and item.h. This
  will be resolved later.
*/
#include "sql_class.h"
#include "set_var.h"
#include "mysqld.h"
#include "lua_call.h"


lua_State *L = NULL;

void bail(lua_State *L, const char *msg)
{
  fprintf(stderr, "\nFATAL ERROR:\n  %s: %s\n\n",
          msg, lua_tostring(L, -1));
  exit(1);
}

int luaadd(int x, int y)
{
  int sum;

  // the function name
  lua_getglobal(L, "add");

  // the first argument
  lua_pushnumber(L, x);

  // the second argument
  lua_pushnumber(L, y);

  // call the function with 2 arguments, return 1 result
  lua_call(L, 2, 1);

  // get the result
  sum = (int)lua_tointeger(L, -1);
  lua_pop(L, 1);

  return sum;
}

int setLuaPath(lua_State* L, const char* path)
{
  lua_getglobal(L, "package");

  // get field "path" from table at top of stack (-1)
  lua_getfield(L, -1, "path");

  // grab path string from top of stack
  std::string cur_path = lua_tostring(L, -1);

  // do your path magic here
  cur_path.append(";");
  cur_path.append(path);

  // get rid of the string on the stack we just pushed on line 5
  lua_pop(L, 1);

  // push the new one
  lua_pushstring(L, cur_path.c_str());

  // set the field "path" in table at -2 with value at top of stack
  lua_setfield(L, -2, "path");

  // get rid of package table from top of stack
  lua_pop(L, 1);

  // all done!
  return 0;
}

void Lua_init()
{
  // Create Lua state variable
  L = luaL_newstate();

  // Load Lua libraries
  luaL_openlibs(L);

  // set lua path
  setLuaPath(L, "/home/pengt/lua/");
}

void Lua_exit()
{
  // Clean up, free the Lua state var
  lua_close(L);

  // Pause
  //printf("Press enter to continue...");
  //getchar();
}

/* The content of /home/pengt/lua/my.lua :

-- tell me
function tellme()
  io.write("This is coming from lua.tellme.\n")
end

-- square
function square(n)
  io.write("Within callfuncscript.lua fcn square, arg=")
  io.write(tostring(n))
  n = n * n
  io.write(", square=")
  io.write(tostring(n))
  print(".")
  return(n)
end

-- add
function add (x, y)
  return x + y
end

print("Priming run")

*/

void Lua_test()
{
  // Load but don't run the Lua script
  if (luaL_loadfile(L, "/home/pengt/lua/my.lua"))
    // Error out if file can't be read
    bail(L, "luaL_loadfile() failed");

  // PRIMING RUN. FORGET THIS AND YOU'RE TOAST
  if (lua_pcall(L, 0, 0, 0))
    // Error out if Lua file has an error
    bail(L, "lua_pcall() failed");

  printf("In C, calling Lua->tellme()\n");

  // Tell it to run callfuncscript.lua->tellme()
  lua_getglobal(L, "tellme");
  // Run the function
  if (lua_pcall(L, 0, 0, 0))
    // Error out if Lua file has an error
    bail(L, "lua_pcall() failed");

  printf("Back in C again\n");
  printf("In C, calling Lua->square(6)\n");

  // Tell it to run callfuncscript.lua->square()
  lua_getglobal(L, "square");
  // Submit 6 as the argument to square()
  lua_pushnumber(L, 6);
  // Run function, !!! NRETURN=1 !!!
  if (lua_pcall(L, 1, 1, 0))
    bail(L, "lua_pcall() failed");

  printf("Back in C again\n");
  int ret = lua_tonumber(L, -1);
  printf("Returned number=%d\n", ret);
}
