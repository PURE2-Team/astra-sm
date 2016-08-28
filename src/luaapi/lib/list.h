/*
 * Astra Lua Library (Module List)
 * http://cesbo.com/astra
 *
 * Copyright (C) 2016, Artem Kharitonov <artem@3phase.pw>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _LUA_LIB_LIST_H_
#define _LUA_LIB_LIST_H_ 1

#ifndef _ASTRA_H_
#   error "Please include <astra.h> first"
#endif /* !_ASTRA_H_ */

#include <luaapi/module.h>

/* extern declarations */
MODULE_MANIFEST_DECL(astra);
MODULE_MANIFEST_DECL(base64);
MODULE_MANIFEST_DECL(iso8859);
MODULE_MANIFEST_DECL(json);
MODULE_MANIFEST_DECL(log);
MODULE_MANIFEST_DECL(md5);
MODULE_MANIFEST_DECL(pidfile);
MODULE_MANIFEST_DECL(rc4);
MODULE_MANIFEST_DECL(sha1);
MODULE_MANIFEST_DECL(strhex);
MODULE_MANIFEST_DECL(timer);
MODULE_MANIFEST_DECL(utils);

/* manifest list */
static const module_manifest_t *lua_lib_list[] =
{
    &MODULE_SYMBOL(astra),
    &MODULE_SYMBOL(base64),
    &MODULE_SYMBOL(iso8859),
    &MODULE_SYMBOL(json),
    &MODULE_SYMBOL(log),
    &MODULE_SYMBOL(md5),
    &MODULE_SYMBOL(pidfile),
    &MODULE_SYMBOL(rc4),
    &MODULE_SYMBOL(sha1),
    &MODULE_SYMBOL(strhex),
    &MODULE_SYMBOL(timer),
    &MODULE_SYMBOL(utils),
    NULL,
};

#endif /* _LUA_LIB_LIST_H_ */