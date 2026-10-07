/* One peer, explicit fixed-endian records, no secret logging. Uncertain reads
 * and writes fail closed. Each write is committed/read-back by the bound KV
 * backend. An empty versioned record is a tombstone, never an implicit format. */
#include "hid_store.h"
#include "host/ble_hs.h"
#include "host/ble_hs_hci.h"
#include <stdbool.h>
#include <string.h>
#define CCC_MAX 6u
static const risc_bound_key_value_v1 *storage;
static struct ble_store_value_sec secs[2];
static bool present[2], failed, local_present;
static struct ble_store_value_cccd ccc[CCC_MAX];
static unsigned ccc_count;
static uint8_t local_irk[16];
static const char *const names[] = {"hid_ours", "hid_peer", "hid_ccc", "hid_identity"};
static uint32_t crc(const uint8_t *b) {
    uint32_t n = ~0u;
    for (unsigned i = 0; i < 60; i++) {
        n ^= b[i];
        for (unsigned j = 0; j < 8; j++)
            n = (n >> 1) ^ (0xedb88320u & -(n & 1u));
    }
    return ~n;
}
static uint32_t u32(const uint8_t *b) {
    return b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static void p32(uint8_t *b, uint32_t n) {
    for (unsigned i = 0; i < 4; i++)
        b[i] = (uint8_t)(n >> (8 * i));
}
static bool address_valid(const ble_addr_t *a) { return a->type <= 1; }
static void base(uint8_t *b, unsigned kind) {
    memset(b, 0, 64);
    b[0] = 1;
    b[1] = (uint8_t)kind;
}
static bool put(unsigned kind, uint8_t *b) {
    p32(b + 60, crc(b));
    if (!storage || storage->put(storage->context, names[kind], b, 64)) {
        failed = true;
        return false;
    }
    return true;
}
static int get(unsigned kind, uint8_t *b) {
    uint32_t n = 0;
    int rc = storage->get(storage->context, names[kind], b, 64, &n);
    if (rc == RISC_BOUND_KEY_VALUE_NOT_FOUND) {
        base(b, kind);
        return 0;
    }
    if (rc || n != 64 || b[0] != 1 || b[1] != kind || u32(b + 60) != crc(b)) {
        failed = true;
        return -1;
    }
    return 1;
}
static bool sec_encode(unsigned idx, const struct ble_store_value_sec *s, bool valid) {
    uint8_t b[64];
    base(b, idx);
    b[2] = valid;
    if (valid) {
        b[3] = s->key_size;
        b[4] = s->peer_addr.type;
        memcpy(b + 5, s->peer_addr.val, 6);
        b[11] = s->ltk_present | (s->irk_present << 1) | (s->authenticated << 2) | (s->sc << 3);
        b[12] = (uint8_t)s->ediv;
        b[13] = (uint8_t)(s->ediv >> 8);
        for (unsigned i = 0; i < 8; i++)
            b[14 + i] = (uint8_t)(s->rand_num >> (8 * i));
        memcpy(b + 22, s->ltk, 16);
        memcpy(b + 38, s->irk, 16);
    }
    bool ok = put(idx, b);
    memset(b, 0, sizeof(b));
    return ok;
}
static bool ccc_save(void) {
    uint8_t b[64];
    base(b, 2);
    b[2] = (uint8_t)ccc_count;
    if (ccc_count) {
        b[4] = ccc[0].peer_addr.type;
        memcpy(b + 5, ccc[0].peer_addr.val, 6);
    }
    for (unsigned i = 0; i < ccc_count; i++) {
        unsigned p = 12 + 6 * i;
        b[p] = (uint8_t)ccc[i].chr_val_handle;
        b[p + 1] = (uint8_t)(ccc[i].chr_val_handle >> 8);
        b[p + 2] = (uint8_t)ccc[i].flags;
        b[p + 3] = (uint8_t)(ccc[i].flags >> 8);
        b[p + 4] = ccc[i].value_changed;
    }
    return put(2, b);
}
void hid_store_clear(void) {
    memset(secs, 0, sizeof(secs));
    memset(present, 0, sizeof(present));
    memset(ccc, 0, sizeof(ccc));
    memset(local_irk, 0, sizeof(local_irk));
    ccc_count = 0;
    failed = local_present = false;
    storage = NULL;
}
bool hid_store_init(const risc_bound_key_value_v1 *s) {
    hid_store_clear();
    storage = s;
    uint8_t b[64];
    for (unsigned i = 0; i < 2; i++) {
        if (get(i, b) < 0)
            goto bad;
        if (b[2] > 1)
            goto bad;
        present[i] = b[2] != 0;
        if (!present[i])
            continue;
        struct ble_store_value_sec *v = &secs[i];
        v->key_size = b[3];
        v->peer_addr.type = b[4];
        memcpy(v->peer_addr.val, b + 5, 6);
        if (!address_valid(&v->peer_addr) || b[11] & 0xf0u || v->key_size > 16 || v->key_size < 7)
            goto bad;
        v->ltk_present = b[11] & 1;
        v->irk_present = (b[11] >> 1) & 1;
        v->authenticated = (b[11] >> 2) & 1;
        v->sc = (b[11] >> 3) & 1;
        v->ediv = b[12] | ((uint16_t)b[13] << 8);
        for (unsigned j = 0; j < 8; j++)
            v->rand_num |= (uint64_t)b[14 + j] << (8 * j);
        memcpy(v->ltk, b + 22, 16);
        memcpy(v->irk, b + 38, 16);
        if (v->ltk_present && (!v->sc || !v->authenticated || v->key_size != 16))
            goto bad;
    }
    if (present[0] && present[1] && ble_addr_cmp(&secs[0].peer_addr, &secs[1].peer_addr))
        goto bad;
    if (get(2, b) < 0 || b[2] > CCC_MAX)
        goto bad;
    ccc_count = b[2];
    for (unsigned i = 0; i < ccc_count; i++) {
        unsigned p = 12 + 6 * i;
        ccc[i].peer_addr.type = b[4];
        memcpy(ccc[i].peer_addr.val, b + 5, 6);
        ccc[i].chr_val_handle = b[p] | ((uint16_t)b[p + 1] << 8);
        ccc[i].flags = b[p + 2] | ((uint16_t)b[p + 3] << 8);
        ccc[i].value_changed = b[p + 4] & 1;
        if (!address_valid(&ccc[i].peer_addr) || !ccc[i].chr_val_handle || ccc[i].flags > 3 ||
            b[p + 4] > 1)
            goto bad;
        for (unsigned j = 0; j < i; j++)
            if (ccc[j].chr_val_handle == ccc[i].chr_val_handle)
                goto bad;
    }
    if (get(3, b) < 0 || b[2] > 1)
        goto bad;
    local_present = b[2];
    if (local_present)
        memcpy(local_irk, b + 4, 16);
    memset(b, 0, sizeof(b));
    return true;
bad:
    failed = true;
    memset(b, 0, sizeof(b));
    return false;
}
bool hid_store_failed(void) { return failed; }
bool hid_store_bonded(void) {
    return !failed && present[0] && secs[0].ltk_present && secs[0].authenticated && secs[0].sc;
}
bool hid_store_peer(const ble_addr_t *a) {
    return !failed && ((present[0] && !ble_addr_cmp(a, &secs[0].peer_addr)) ||
                       (present[1] && !ble_addr_cmp(a, &secs[1].peer_addr)));
}
static bool matches(const ble_addr_t *a, const ble_addr_t *b) {
    return !ble_addr_cmp(a, BLE_ADDR_ANY) || !ble_addr_cmp(a, b);
}
int hid_store_read(int type, const union ble_store_key *k, union ble_store_value *v) {
    if (failed)
        return BLE_HS_ESTORE_FAIL;
    if (type == BLE_STORE_OBJ_TYPE_OUR_SEC || type == BLE_STORE_OBJ_TYPE_PEER_SEC) {
        unsigned i = (unsigned)type - 1;
        if (present[i] && !k->sec.idx && matches(&k->sec.peer_addr, &secs[i].peer_addr)) {
            v->sec = secs[i];
            return 0;
        }
    } else if (type == BLE_STORE_OBJ_TYPE_CCCD) {
        unsigned skip = k->cccd.idx;
        for (unsigned i = 0; i < ccc_count; i++)
            if (matches(&k->cccd.peer_addr, &ccc[i].peer_addr) &&
                (!k->cccd.chr_val_handle || k->cccd.chr_val_handle == ccc[i].chr_val_handle)) {
                if (skip) {
                    skip--;
                    continue;
                }
                v->cccd = ccc[i];
                return 0;
            }
    } else
        return BLE_HS_ENOTSUP;
    return BLE_HS_ENOENT;
}
int hid_store_write(int type, const union ble_store_value *v) {
    if (failed)
        return BLE_HS_ESTORE_FAIL;
    if (type == BLE_STORE_OBJ_TYPE_OUR_SEC || type == BLE_STORE_OBJ_TYPE_PEER_SEC) {
        unsigned i = (unsigned)type - 1;
        const struct ble_store_value_sec *s = &v->sec;
        if (!address_valid(&s->peer_addr) || s->csrk_present || s->key_size != 16 ||
            (s->ltk_present && (!s->sc || !s->authenticated)))
            return BLE_HS_EINVAL;
        for (unsigned j = 0; j < 2; j++)
            if (present[j] && ble_addr_cmp(&secs[j].peer_addr, &s->peer_addr))
                return BLE_HS_ESTORE_CAP;
        if (!sec_encode(i, s, true))
            return BLE_HS_ESTORE_FAIL;
        secs[i] = *s;
        present[i] = true;
        return 0;
    }
    if (type == BLE_STORE_OBJ_TYPE_CCCD) {
        if (!address_valid(&v->cccd.peer_addr) || !v->cccd.chr_val_handle || v->cccd.flags > 3)
            return BLE_HS_EINVAL;
        if (!hid_store_peer(&v->cccd.peer_addr))
            return BLE_HS_ENOENT;
        unsigned i;
        for (i = 0; i < ccc_count; i++) {
            if (ble_addr_cmp(&ccc[i].peer_addr, &v->cccd.peer_addr))
                return BLE_HS_ESTORE_CAP;
            if (ccc[i].chr_val_handle == v->cccd.chr_val_handle)
                break;
        }
        if (i == CCC_MAX)
            return BLE_HS_ESTORE_CAP;
        if (i == ccc_count)
            ccc_count++;
        ccc[i] = v->cccd;
        return ccc_save() ? 0 : BLE_HS_ESTORE_FAIL;
    }
    return BLE_HS_ENOTSUP;
}
int hid_store_delete(int type, const union ble_store_key *k) {
    if (failed)
        return BLE_HS_ESTORE_FAIL;
    if (type == BLE_STORE_OBJ_TYPE_OUR_SEC || type == BLE_STORE_OBJ_TYPE_PEER_SEC) {
        unsigned i = (unsigned)type - 1;
        if (!present[i] || !matches(&k->sec.peer_addr, &secs[i].peer_addr))
            return BLE_HS_ENOENT;
        if (!sec_encode(i, NULL, false))
            return BLE_HS_ESTORE_FAIL;
        memset(&secs[i], 0, sizeof(secs[i]));
        present[i] = false;
        return 0;
    }
    if (type == BLE_STORE_OBJ_TYPE_CCCD) {
        for (unsigned i = 0; i < ccc_count; i++)
            if (matches(&k->cccd.peer_addr, &ccc[i].peer_addr) &&
                (!k->cccd.chr_val_handle || k->cccd.chr_val_handle == ccc[i].chr_val_handle)) {
                memmove(&ccc[i], &ccc[i + 1], (ccc_count - i - 1) * sizeof(ccc[0]));
                ccc_count--;
                memset(&ccc[ccc_count], 0, sizeof(ccc[0]));
                return ccc_save() ? 0 : BLE_HS_ESTORE_FAIL;
            }
        return BLE_HS_ENOENT;
    }
    return BLE_HS_ENOTSUP;
}
bool hid_store_forget(void) {
    /* Clear CCCs first. Any partial failure remains closed until retry. */
    failed = false;
    ccc_count = 0;
    if (!ccc_save() || !sec_encode(0, NULL, false) || !sec_encode(1, NULL, false)) {
        failed = true;
        return false;
    }
    uint8_t b[64];
    base(b, 3);
    if (!put(3, b))
        return false;
    local_present = false;
    memset(local_irk, 0, sizeof(local_irk));
    memset(secs, 0, sizeof(secs));
    memset(present, 0, sizeof(present));
    memset(ccc, 0, sizeof(ccc));
    return true;
}
int hid_store_key(uint8_t key, struct ble_store_gen_key *v, uint16_t conn) {
    (void)conn;
    if (failed)
        return BLE_HS_ESTORE_FAIL;
    if (key != BLE_STORE_GEN_KEY_IRK)
        return BLE_HS_ENOTSUP;
    if (!local_present) {
        if (ble_hs_hci_rand(local_irk, 16)) {
            failed = true;
            return BLE_HS_EUNKNOWN;
        }
        uint8_t b[64];
        base(b, 3);
        b[2] = 1;
        memcpy(b + 4, local_irk, 16);
        bool ok = put(3, b);
        memset(b, 0, sizeof(b));
        if (!ok)
            return BLE_HS_ESTORE_FAIL;
        local_present = true;
    }
    memcpy(v->irk, local_irk, 16);
    return 0;
}
