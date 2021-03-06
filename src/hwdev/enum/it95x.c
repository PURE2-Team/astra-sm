/*
 * Astra Module: Hardware Enumerator (IT95x)
 *
 * Copyright (C) 2017, Artem Kharitonov <artem@3phase.pw>
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

#include "enum.h"
#include "../it95x/api.h"

static
int it95x_enumerate(lua_State *L)
{
    size_t cnt = 0;
    int ret = it95x_dev_count(&cnt);

    if (ret != 0)
    {
        char *const err = it95x_strerror(ret);
        lua_pushfstring(L, "couldn't retrieve device count: %s", err);
        free(err);
        lua_error(L);
    }

    lua_newtable(L);

    for (size_t i = 0; i < cnt; i++)
    {
        lua_newtable(L);
        lua_pushinteger(L, i);
        lua_setfield(L, -2, "adapter");

        it95x_dev_t *dev = NULL;
        ret = it95x_open(i, NULL, &dev);

        if (ret == 0)
        {
            it95x_dev_info_t info;
            it95x_get_info(dev, &info);

            lua_pushstring(L, info.name);
            lua_setfield(L, -2, "name");

            lua_pushstring(L, info.devpath);
            lua_setfield(L, -2, "devpath");

            char buf[16] = { 0 };
            snprintf(buf, sizeof(buf), "%08x", info.drv_version);
            lua_pushstring(L, buf);
            lua_setfield(L, -2, "drv_version");

            snprintf(buf, sizeof(buf), "%08x", info.fw_link);
            lua_pushstring(L, buf);
            lua_setfield(L, -2, "fw_link");

            snprintf(buf, sizeof(buf), "%08x", info.fw_ofdm);
            lua_pushstring(L, buf);
            lua_setfield(L, -2, "fw_ofdm");

            if (asc_log_is_debug())
            {
                snprintf(buf, sizeof(buf), "%08x", info.drv_pid);
                lua_pushstring(L, buf);
                lua_setfield(L, -2, "drv_pid");

                snprintf(buf, sizeof(buf), "0x%04x", info.chip_type);
                lua_pushstring(L, buf);
                lua_setfield(L, -2, "type_chip");

                snprintf(buf, sizeof(buf), "0x%02x", info.dev_type);
                lua_pushstring(L, buf);
                lua_setfield(L, -2, "type_dev");

                snprintf(buf, sizeof(buf), "0x%02x", info.tuner_id);
                lua_pushstring(L, buf);
                lua_setfield(L, -2, "type_tuner");

                switch(info.usb_mode)
                {
                    case IT95X_USB_11: lua_pushstring(L, "USB 1.1"); break;
                    case IT95X_USB_20: lua_pushstring(L, "USB 2.0"); break;
                    default:
                        lua_pushstring(L, "unknown");
                        break;
                }
                lua_setfield(L, -2, "usb_mode");

                snprintf(buf, sizeof(buf), "0x%04x", info.vendor_id);
                lua_pushstring(L, buf);
                lua_setfield(L, -2, "usb_vid");

                snprintf(buf, sizeof(buf), "0x%04x", info.product_id);
                lua_pushstring(L, buf);
                lua_setfield(L, -2, "usb_pid");
            }

            it95x_close(dev);
        }
        else
        {
            char *const err = it95x_strerror(ret);
            lua_pushfstring(L, "couldn't open device: %s", err);
            lua_setfield(L, -2, "error");
            free(err);
        }

        const int pos = luaL_len(L, -2) + 1;
        lua_rawseti(L, -2, pos);
    }

    return 1;
}

HW_ENUM_REGISTER(it95x)
{
    .name = "it95x_output",
    .description = "ITE IT9500 Series Modulators",
    .enumerate = it95x_enumerate,
};
