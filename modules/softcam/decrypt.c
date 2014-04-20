/*
 * Astra Module: SoftCAM
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2014, Andrey Dyldin <and@cesbo.com>
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

/*
 * Module Name:
 *      decrypt
 *
 * Module Options:
 *      upstream    - object, stream instance returned by module_instance:stream()
 *      name        - string, channel name
 *      biss        - string, BISS key, 16 chars length. example: biss = "1122330044556600"
 *      cam         - object, cam instance returned by cam_module_instance:cam()
 *      cas_data    - string, additional paramters for CAS
 *      cas_pnr     - number, original PNR
 */

#include <astra.h>
#include "module_cam.h"
#include "cas/cas_list.h"
#include <dvbcsa/dvbcsa.h>

typedef struct
{
    module_data_t *mod;

    uint8_t ecm_type;
    uint16_t ecm_pid;

    uint8_t parity;
    struct dvbcsa_bs_key_s *even_key;
    struct dvbcsa_bs_key_s *odd_key;

    int new_key_id;  // 0 - not, 1 - first key, 2 - second key, 3 - both keys
    uint8_t new_key[16];

    struct dvbcsa_bs_batch_s *batch;
    size_t batch_skip;
} ca_stream_t;

typedef struct
{
    uint16_t es_pid;

    ca_stream_t *ca_stream;
} el_stream_t;

struct module_data_t
{
    MODULE_STREAM_DATA();
    MODULE_DECRYPT_DATA();

    /* Config */
    const char *name;
    int caid;

    /* dvbcsa */
    asc_list_t *el_list;
    asc_list_t *ca_list;

    size_t batch_size;
    size_t storage_size;
    size_t storage_skip;

    uint8_t *batch_storage_recv;
    uint8_t *batch_storage_send;

    /* Base */
    mpegts_psi_t *stream[MAX_PID];
    mpegts_psi_t *pmt;
};

#define MSG(_msg) "[decrypt %s] " _msg, mod->name

static module_cas_t * module_decrypt_cas_init(module_data_t *mod)
{
    for(int i = 0; cas_init_list[i]; ++i)
    {
        module_cas_t *cas = cas_init_list[i](&mod->__decrypt);
        if(cas)
            return cas;
    }
    return NULL;
}

static void module_decrypt_cas_destroy(module_data_t *mod)
{
    if(!mod->__decrypt.cas)
        return;
    free(mod->__decrypt.cas->self);
    mod->__decrypt.cas = NULL;
}

static void stream_reload(module_data_t *mod)
{
    mod->stream[0]->crc32 = 0;

    for(int i = 1; i < MAX_PID; ++i)
    {
        if(mod->stream[i])
        {
            mpegts_psi_destroy(mod->stream[i]);
            mod->stream[i] = NULL;
        }
    }

    module_decrypt_cas_destroy(mod);
}

ca_stream_t * ca_stream_init(module_data_t *mod, uint16_t ecm_pid)
{
    ca_stream_t *ca_stream;
    asc_list_for(mod->ca_list)
    {
        ca_stream = asc_list_data(mod->ca_list);
        if(ca_stream->ecm_pid == ecm_pid)
            return ca_stream;
    }

    ca_stream = malloc(sizeof(ca_stream_t));

    ca_stream->ecm_pid = ecm_pid;

    ca_stream->parity = 0x00;
    ca_stream->even_key = dvbcsa_bs_key_alloc();
    ca_stream->odd_key = dvbcsa_bs_key_alloc();

    ca_stream->batch = calloc(mod->batch_size + 1, sizeof(struct dvbcsa_bs_batch_s));
    ca_stream->batch_skip = 0;

    asc_list_insert_tail(mod->ca_list, ca_stream);

    return ca_stream;
}

void ca_stream_destroy(ca_stream_t *ca_stream)
{
    dvbcsa_bs_key_free(ca_stream->even_key);
    dvbcsa_bs_key_free(ca_stream->odd_key);
    free(ca_stream->batch);
    free(ca_stream);
}

void ca_stream_set_keys(ca_stream_t *ca_stream, const uint8_t *even, const uint8_t *odd)
{
    if(even)
        dvbcsa_bs_key_set(even, ca_stream->even_key);
    if(odd)
        dvbcsa_bs_key_set(odd, ca_stream->odd_key);
}

ca_stream_t * ca_stream_get(module_data_t *mod, uint16_t es_pid)
{
    asc_list_for(mod->el_list)
    {
        el_stream_t *el_stream = asc_list_data(mod->el_list);
        if(el_stream->es_pid == es_pid)
            return el_stream->ca_stream;
    }

    asc_list_first(mod->el_list);
    el_stream_t *el_stream = asc_list_data(mod->el_list);
    return el_stream->ca_stream;
}

/*
 * oooooooooo   o   ooooooooooo
 *  888    888 888  88  888  88
 *  888oooo88 8  88     888
 *  888      8oooo88    888
 * o888o   o88o  o888o o888o
 *
 */

static void on_pat(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("PAT checksum mismatch"));
        return;
    }

    // reload stream
    if(psi->crc32 != 0)
    {
        asc_log_warning(MSG("PAT changed. Reload stream info"));
        stream_reload(mod);
    }

    psi->crc32 = crc32;

    const uint8_t *pointer = PAT_ITEMS_FIRST(psi);
    while(!PAT_ITEMS_EOL(psi, pointer))
    {
        const uint16_t pnr = PAT_ITEM_GET_PNR(psi, pointer);
        if(pnr)
        {
            mod->__decrypt.pnr = pnr;
            if(mod->__decrypt.cas_pnr == 0)
                mod->__decrypt.cas_pnr = pnr;
            const uint16_t pmt_pid = PAT_ITEM_GET_PID(psi, pointer);
            if(!mod->stream[pmt_pid])
                mod->stream[pmt_pid] = mpegts_psi_init(MPEGTS_PACKET_PMT, pmt_pid);
            else
                asc_log_warning(MSG("Skip PMT pid:%d"), pmt_pid);
            break;
        }
        PAT_ITEMS_NEXT(psi, pointer);
    }

    if(mod->__decrypt.cam && mod->__decrypt.cam->is_ready)
    {
        mod->__decrypt.cas = module_decrypt_cas_init(mod);
        asc_assert(mod->__decrypt.cas != NULL, MSG("CAS with CAID:0x%04X not found"), mod->caid);
        mod->stream[1] = mpegts_psi_init(MPEGTS_PACKET_CAT, 1);
    }
}

/*
 *   oooooooo8     o   ooooooooooo
 * o888     88    888  88  888  88
 * 888           8  88     888
 * 888o     oo  8oooo88    888
 *  888oooo88 o88o  o888o o888o
 *
 */

static bool __cat_check_desc(module_data_t *mod, const uint8_t *desc)
{
    const uint16_t pid = DESC_CA_PID(desc);

    /* Skip BISS */
    if(pid == NULL_TS_PID)
        return false;

    if(mod->stream[pid])
    {
        if(!(mod->stream[pid]->type & MPEGTS_PACKET_CA))
        {
            asc_log_warning(MSG("Skip EMM pid:%d"), pid);
            return false;
        }
    }
    else
    {
        mod->stream[pid] = mpegts_psi_init(MPEGTS_PACKET_CA, pid);
        if(mod->__decrypt.cam->disable_emm)
            return false;
    }

    if(   mod->__decrypt.cas
       && DESC_CA_CAID(desc) == mod->caid
       && module_cas_check_descriptor(mod->__decrypt.cas, desc))
    {
        mod->stream[pid]->type = MPEGTS_PACKET_EMM;
        asc_log_info(MSG("Select EMM pid:%d"), pid);
        return true;
    }

    return false;
}

static void on_cat(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("CAT checksum mismatch"));
        return;
    }

    // reload stream
    if(psi->crc32 != 0)
    {
        asc_log_warning(MSG("CAT changed. Reload stream info"));
        stream_reload(mod);
        return;
    }

    psi->crc32 = crc32;

    bool is_emm_selected = mod->__decrypt.cam->disable_emm;

    const uint8_t *desc_pointer;
    CAT_DESC_FOREACH(psi, desc_pointer)
    {
        if(desc_pointer[0] == 0x09)
        {
            if(__cat_check_desc(mod, desc_pointer))
                is_emm_selected = true;
        }
    }

    if(!is_emm_selected)
        asc_log_warning(MSG("EMM is not found"));
}

/*
 * oooooooooo oooo     oooo ooooooooooo
 *  888    888 8888o   888  88  888  88
 *  888oooo88  88 888o8 88      888
 *  888        88  888  88      888
 * o888o      o88o  8  o88o    o888o
 *
 */

static bool __pmt_check_desc(module_data_t *mod, const uint8_t *desc, bool is_ecm_selected)
{
    const uint16_t pid = DESC_CA_PID(desc);

    /* Skip BISS */
    if(pid == NULL_TS_PID)
        return false;

    if(mod->stream[pid])
    {
        if(!(mod->stream[pid]->type & MPEGTS_PACKET_CA))
        {
            asc_log_warning(MSG("Skip ECM pid:%d"), pid);
            return false;
        }
    }
    else
        mod->stream[pid] = mpegts_psi_init(MPEGTS_PACKET_CA, pid);

    if(   mod->__decrypt.cas
       && DESC_CA_CAID(desc) == mod->caid
       && module_cas_check_descriptor(mod->__decrypt.cas, desc))
    {
        if(is_ecm_selected)
        {
            asc_log_warning(MSG("Skip ECM pid:%d"), pid);
            return 0;
        }

        asc_list_for(mod->ca_list)
        {
            ca_stream_t *ca_stream = asc_list_data(mod->ca_list);
            if(ca_stream->ecm_pid == pid)
                return true;
        }

        ca_stream_init(mod, pid);
        mod->stream[pid]->type = MPEGTS_PACKET_ECM;
        asc_log_info(MSG("Select ECM pid:%d"), pid);
        return true;
    }

    return false;
}

static void on_pmt(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    if(psi->buffer[0] != 0x02)
        return;

    // check pnr
    const uint16_t pnr = PMT_GET_PNR(psi);
    if(pnr != mod->__decrypt.pnr)
        return;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
    {
        mpegts_psi_demux(mod->pmt
                         , (void (*)(void *, const uint8_t *))__module_stream_send
                         , &mod->__stream);
        return;
    }

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("PMT checksum mismatch"));
        return;
    }

    // reload stream
    if(psi->crc32 != 0)
    {
        asc_log_warning(MSG("PMT changed. Reload stream info"));
        stream_reload(mod);
        return;
    }

    psi->crc32 = crc32;

    // Make custom PMT and set descriptors for CAS
    mod->pmt->pid = psi->pid;

    bool is_ecm_selected;

    uint16_t skip = 12;
    memcpy(mod->pmt->buffer, psi->buffer, 10);

    is_ecm_selected = false;
    const uint8_t *desc_pointer;
    PMT_DESC_FOREACH(psi, desc_pointer)
    {
        if(desc_pointer[0] == 0x09)
        {
            if(__pmt_check_desc(mod, desc_pointer, is_ecm_selected))
                is_ecm_selected = true;
        }
        else
        {
            const uint8_t size = desc_pointer[1] + 2;
            memcpy(&mod->pmt->buffer[skip], desc_pointer, size);
            skip += size;
        }
    }
    const uint16_t size = skip - 12; // 12 - PMT header
    mod->pmt->buffer[10] = (psi->buffer[10] & 0xF0) | ((size >> 8) & 0x0F);
    mod->pmt->buffer[11] = size & 0xFF;

    const uint8_t *pointer;
    PMT_ITEMS_FOREACH(psi, pointer)
    {
        memcpy(&mod->pmt->buffer[skip], pointer, 5);
        skip += 5;

        const uint16_t skip_last = skip;

        is_ecm_selected = false;
        PMT_ITEM_DESC_FOREACH(pointer, desc_pointer)
        {
            if(desc_pointer[0] == 0x09)
            {
                if(__pmt_check_desc(mod, desc_pointer, is_ecm_selected))
                    is_ecm_selected = true;
            }
            else
            {
                const uint8_t size = desc_pointer[1] + 2;
                memcpy(&mod->pmt->buffer[skip], desc_pointer, size);
                skip += size;
            }
        }
        const uint16_t size = skip - skip_last;
        mod->pmt->buffer[skip_last - 2] = (size << 8) & 0x0F;
        mod->pmt->buffer[skip_last - 1] = size & 0xFF;
    }

    mod->pmt->buffer_size = skip + CRC32_SIZE;
    PSI_SET_SIZE(mod->pmt);
    PSI_SET_CRC32(mod->pmt);

    mpegts_psi_demux(mod->pmt
                     , (void (*)(void *, const uint8_t *))__module_stream_send
                     , &mod->__stream);
}

/*
 * ooooooooooo oooo     oooo
 *  888    88   8888o   888
 *  888ooo8     88 888o8 88
 *  888    oo   88  888  88
 * o888ooo8888 o88o  8  o88o
 *
 */

static void on_em(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    if(!mod->__decrypt.cam->is_ready)
        return;

    if(psi->buffer_size > EM_MAX_SIZE)
    {
        asc_log_error(MSG("Entitlement message size is greater than %d"), EM_MAX_SIZE);
        return;
    }

    ca_stream_t *ca_stream = NULL;

    const uint8_t em_type = psi->buffer[0];

    if(em_type == 0x80 || em_type == 0x81)
    { /* ECM */
        asc_list_for(mod->ca_list)
        {
            ca_stream_t *i = asc_list_data(mod->ca_list);
            if(i->ecm_pid == psi->pid)
            {
                ca_stream = i;
                break;
            }

        }

        if(!ca_stream)
            return;

        if(em_type == ca_stream->ecm_type)
            return;

        if(!module_cas_check_em(mod->__decrypt.cas, psi))
            return;

        ca_stream->ecm_type = em_type;
    }
    else if(em_type >= 0x82 && em_type <= 0x8F)
    { /* EMM */
        if(mod->__decrypt.cam->disable_emm)
            return;

        if(!module_cas_check_em(mod->__decrypt.cas, psi))
            return;
    }
    else
    {
        asc_log_error(MSG("wrong packet type 0x%02X"), em_type);
        return;
    }

    mod->__decrypt.cam->send_em(  mod->__decrypt.cam->self
                                , &mod->__decrypt, ca_stream
                                , psi->buffer, psi->buffer_size);
}

/*
 * ooooooooooo  oooooooo8
 * 88  888  88 888
 *     888      888oooooo
 *     888             888
 *    o888o    o88oooo888
 *
 */

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    const uint16_t pid = TS_PID(ts);

    if(pid == 0)
    {
        mpegts_psi_mux(mod->stream[pid], ts, on_pat, mod);
    }
    else if(pid == 1)
    {
        if(mod->stream[pid])
            mpegts_psi_mux(mod->stream[pid], ts, on_cat, mod);
        return;
    }
    else if(pid == NULL_TS_PID)
    {
        return;
    }
    else if(mod->stream[pid])
    {
        switch(mod->stream[pid]->type)
        {
            case MPEGTS_PACKET_PMT:
                mpegts_psi_mux(mod->stream[pid], ts, on_pmt, mod);
                return;
            case MPEGTS_PACKET_ECM:
            case MPEGTS_PACKET_EMM:
                mpegts_psi_mux(mod->stream[pid], ts, on_em, mod);
            case MPEGTS_PACKET_CA:
                return;
            default:
                break;
        }
    }

    if(asc_list_size(mod->ca_list) == 0)
    {
        module_stream_send(mod, ts);
        return;
    }

    uint8_t *dst = &mod->batch_storage_recv[mod->storage_skip];
    memcpy(dst, ts, TS_PACKET_SIZE);

    uint8_t hdr_size = 0;
    const uint8_t sc = TS_SC(dst);

    if(sc)
    {
        switch(TS_AF(ts))
        {
            case 0x10:
                hdr_size = 4;
                break;
            case 0x30:
            {
                hdr_size = 4 + dst[4] + 1;
                if(hdr_size < TS_PACKET_SIZE)
                    break;
            }
            default:
                break;
        }

        if(hdr_size)
        {
            ca_stream_t *ca_stream = NULL;
            asc_list_for(mod->el_list)
            {
                el_stream_t *el_stream = asc_list_data(mod->el_list);
                if(el_stream->es_pid == pid)
                {
                    ca_stream = el_stream->ca_stream;
                    break;
                }
            }
            if(!ca_stream)
            {
                asc_list_first(mod->ca_list);
                ca_stream = asc_list_data(mod->ca_list);
            }

            if(ca_stream->parity == 0x00)
                ca_stream->parity = sc;

            dst[3] &= ~0xC0;
            ca_stream->batch[ca_stream->batch_skip].data = &dst[hdr_size];
            ca_stream->batch[ca_stream->batch_skip].len = TS_PACKET_SIZE - hdr_size;
            ++ca_stream->batch_skip;
        }
    }

    if(mod->batch_storage_send)
        module_stream_send(mod, &mod->batch_storage_send[mod->storage_skip]);
    mod->storage_skip += TS_PACKET_SIZE;

    if(mod->storage_skip >= mod->storage_size)
    {
        asc_list_for(mod->ca_list)
        {
            ca_stream_t *ca_stream = asc_list_data(mod->ca_list);
            ca_stream->batch[ca_stream->batch_skip].data = NULL;

            if(ca_stream->parity == 0x80)
                dvbcsa_bs_decrypt(ca_stream->even_key, ca_stream->batch, TS_BODY_SIZE);
            else if(ca_stream->parity == 0xC0)
                dvbcsa_bs_decrypt(ca_stream->odd_key, ca_stream->batch, TS_BODY_SIZE);

            ca_stream->batch_skip = 0;
            ca_stream->parity = 0x00;

            // check new key
            if(ca_stream->new_key_id == 1)
                ca_stream_set_keys(ca_stream, &ca_stream->new_key[0], NULL);
            else if(ca_stream->new_key_id == 2)
                ca_stream_set_keys(ca_stream, NULL, &ca_stream->new_key[8]);
            else if(ca_stream->new_key_id == 3)
                ca_stream_set_keys(ca_stream, &ca_stream->new_key[0], &ca_stream->new_key[8]);

            ca_stream->new_key_id = 0;
        }

        uint8_t *storage_tmp = mod->batch_storage_send;
        mod->batch_storage_send = mod->batch_storage_recv;
        if(!storage_tmp)
            storage_tmp = malloc(mod->storage_size);
        mod->batch_storage_recv = storage_tmp;
        mod->storage_skip = 0;
    }
}

/*
 *      o      oooooooooo ooooo
 *     888      888    888 888
 *    8  88     888oooo88  888
 *   8oooo88    888        888
 * o88o  o888o o888o      o888o
 *
 */

void on_cam_ready(module_data_t *mod)
{
    mod->caid = mod->__decrypt.cam->caid;
    stream_reload(mod);
}

void on_cam_error(module_data_t *mod)
{
    mod->caid = 0x0000;

    module_decrypt_cas_destroy(mod);

    asc_list_for(mod->ca_list)
    {
        ca_stream_t *ca_stream = asc_list_data(mod->ca_list);
        ca_stream->new_key_id = 0;
        ca_stream->batch_skip = 0;
    }
}

void on_cam_response(module_data_t *mod, void *arg, const uint8_t *data, const char *errmsg)
{
    ca_stream_t *ca_stream = arg;

    if((data[0] & ~0x01) != 0x80)
        return; /* Skip EMM */

    if(!mod->__decrypt.cas)
        return; /* after stream_reload */

    bool is_keys_ok = false;
    do
    {
        if(errmsg)
            break;

        if(!module_cas_check_keys(mod->__decrypt.cas, data))
        {
            errmsg = "Wrong ECM id";
            break;
        }

        if(data[2] != 16)
        {
            errmsg = (data[2] == 0) ? "" : "Wrong ECM length";
            break;
        }

        static const char *errmsg_checksum = "Wrong ECM checksum";
        const uint8_t ck1 = (data[3] + data[4] + data[5]) & 0xFF;
        if(ck1 != data[6])
        {
            errmsg = errmsg_checksum;
            break;
        }

        const uint8_t ck2 = (data[7] + data[8] + data[9]) & 0xFF;
        if(ck2 != data[10])
        {
            errmsg = errmsg_checksum;
            break;
        }

        is_keys_ok = true;
    } while(0);

    if(is_keys_ok)
    {
        // Set keys
        if(ca_stream->new_key[11] == data[14] && ca_stream->new_key[15] == data[18])
        {
            ca_stream->new_key_id = 1;
            memcpy(&ca_stream->new_key[0], &data[3], 8);
        }
        else if(ca_stream->new_key[3] == data[6] && ca_stream->new_key[7] == data[10])
        {
            ca_stream->new_key_id = 2;
            memcpy(&ca_stream->new_key[8], &data[11], 8);
        }
        else
        {
            ca_stream->new_key_id = 3;
            memcpy(ca_stream->new_key, &data[3], 16);
            asc_log_warning(MSG("Both keys changed"));
        }

#if CAS_ECM_DUMP
        char key_1[17], key_2[17];
        hex_to_str(key_1, &data[3], 8);
        hex_to_str(key_2, &data[11], 8);
        asc_log_debug(MSG("ECM Found [%02X:%s:%s]") , data[0], key_1, key_2);
#endif
    }
    else
    {
        if(!errmsg)
            errmsg = "Unknown";
        asc_log_error(MSG("ECM:0x%02X size:%d Not Found. %s") , data[0], data[2], errmsg);
    }
}

/*
 * oooo     oooo  ooooooo  ooooooooo  ooooo  oooo ooooo       ooooooooooo
 *  8888o   888 o888   888o 888    88o 888    88   888         888    88
 *  88 888o8 88 888     888 888    888 888    88   888         888ooo8
 *  88  888  88 888o   o888 888    888 888    88   888      o  888    oo
 * o88o  8  o88o  88ooo88  o888ooo88    888oo88   o888ooooo88 o888ooo8888
 *
 */

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, on_ts);

    mod->__decrypt.self = mod;

    module_option_string("name", &mod->name, NULL);
    asc_assert(mod->name != NULL, "[decrypt] option 'name' is required");

    mod->stream[0] = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
    mod->pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, MAX_PID);

    mod->ca_list = asc_list_init();
    mod->el_list = asc_list_init();

    mod->batch_size = dvbcsa_bs_batch_size();

    mod->storage_size = mod->batch_size * TS_PACKET_SIZE;
    mod->batch_storage_recv = malloc(mod->storage_size);

    const char *biss_key = NULL;
    size_t biss_length = 0;
    module_option_string("biss", &biss_key, &biss_length);
    if(biss_key)
    {
        asc_assert(biss_length == 16, MSG("biss key must be 16 char length"));

        uint8_t key[8];
        str_to_hex(biss_key, key, sizeof(key));
        key[3] = (key[0] + key[1] + key[2]) & 0xFF;
        key[7] = (key[4] + key[5] + key[6]) & 0xFF;
        mod->caid = 0x2600;

        ca_stream_t *biss = ca_stream_init(mod, NULL_TS_PID);
        ca_stream_set_keys(biss, key, key);
    }

    lua_getfield(lua, 2, "cam");
    if(!lua_isnil(lua, -1))
    {
        asc_assert(  lua_type(lua, -1) == LUA_TLIGHTUSERDATA
                   , "option 'cam' required cam-module instance");
        mod->__decrypt.cam = lua_touserdata(lua, -1);

        int cas_pnr = 0;
        module_option_number("cas_pnr", &cas_pnr);
        if(cas_pnr > 0 && cas_pnr <= 0xFFFF)
            mod->__decrypt.cas_pnr = (uint16_t)cas_pnr;

        const char *cas_data = NULL;
        module_option_string("cas_data", &cas_data, NULL);
        if(cas_data)
        {
            mod->__decrypt.is_cas_data = true;
            str_to_hex(cas_data, mod->__decrypt.cas_data, sizeof(mod->__decrypt.cas_data));
        }

        module_cam_attach_decrypt(mod->__decrypt.cam, &mod->__decrypt);
    }
    lua_pop(lua, 1);

    stream_reload(mod);
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    if(mod->__decrypt.cam)
    {
        module_cam_detach_decrypt(mod->__decrypt.cam, &mod->__decrypt);
        module_decrypt_cas_destroy(mod);
    }

    for(  asc_list_first(mod->ca_list)
        ; !asc_list_eol(mod->ca_list)
        ; asc_list_remove_current(mod->ca_list))
    {
        ca_stream_t *ca_stream = asc_list_data(mod->ca_list);
        ca_stream_destroy(ca_stream);
    }
    asc_list_destroy(mod->ca_list);

    for(  asc_list_first(mod->el_list)
        ; !asc_list_eol(mod->el_list)
        ; asc_list_remove_current(mod->el_list))
    {
        el_stream_t *el_stream = asc_list_data(mod->el_list);
        free(el_stream);
    }
    asc_list_destroy(mod->el_list);

    free(mod->batch_storage_recv);
    if(mod->batch_storage_send)
        free(mod->batch_storage_send);

    for(int i = 0; i < MAX_PID; ++i)
    {
        if(mod->stream[i])
            mpegts_psi_destroy(mod->stream[i]);
    }
    mpegts_psi_destroy(mod->pmt);
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(decrypt)
