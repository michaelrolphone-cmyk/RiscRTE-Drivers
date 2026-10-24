/* Private, bounded HID parser. Included by the actual production driver. */
#define MOUSE_FIELDS 36u
#define MOUSE_REPORT_IDS 8u
#define MOUSE_GLOBAL_STACK 4u
#define MOUSE_USAGES 32u
#define MOUSE_COLLECTION_DEPTH 8u

typedef struct {
    uint16_t bit;
    uint8_t size, kind; /* 1..4 x/y/wheel/pan, 5 button */
    int32_t minimum, maximum;
    uint8_t index;
} mouse_field;
typedef struct { uint8_t id, bytes; } mouse_report;
typedef struct {
    mouse_field fields[MOUSE_FIELDS];
    mouse_report reports[MOUSE_REPORT_IDS];
    uint8_t field_count, report_count, report_id;
    bool boot;
} mouse_layout;
typedef struct {
    uint32_t page, size, count, id;
    int32_t minimum, maximum;
    bool minimum_set, maximum_set;
} mouse_globals;

static uint32_t unsigned_item(const uint8_t *p, size_t n) {
    uint32_t v = 0;
    for (size_t i = 0; i < n; ++i) v |= (uint32_t)p[i] << (8u * i);
    return v;
}
static int32_t signed_item(uint32_t value, size_t n) {
    if (n && n < 4 && (value & (1u << (n * 8u - 1u))))
        return (int32_t)value - (int32_t)(1u << (n * 8u));
    /* Avoid implementation-defined conversion for negative 32-bit items. */
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(~value);
}
static bool usage_value(uint32_t page, uint32_t value, size_t n, uint32_t *out) {
    if (n == 4) { *out = value; return true; }
    if (page > 65535u || value > 65535u) return false;
    *out = (page << 16u) | value;
    return true;
}
static bool parse_layout(mouse_layout *out, const uint8_t *data, size_t length) {
    if (!out || !data || !length || length > RISC_USB_HID_MAX_DESCRIPTOR) return false;
    mouse_layout layout = {0};
    mouse_globals g = {0}, stack[MOUSE_GLOBAL_STACK];
    uint16_t positions[256] = {0};
    uint32_t usages[MOUSE_USAGES], lo = 0, hi = 0;
    unsigned sp = 0, depth = 0, used = 0, mouse_depth = 0, mouse_apps = 0;
    uint32_t button_mask = 0;
    uint8_t axis_mask = 0;
    bool lo_set = false, hi_set = false, chosen = false, ids = false;
    for (size_t at = 0; at < length;) {
        uint8_t prefix = data[at++];
        if (prefix == 0xfe) return false; /* long items deliberately unsupported */
        size_t n = prefix & 3u;
        if (n == 3) n = 4;
        if (n > length - at) return false;
        uint32_t v = unsigned_item(data + at, n);
        int32_t sv = signed_item(v, n);
        at += n;
        uint8_t type = (prefix >> 2u) & 3u, tag = prefix >> 4u;
        if (type == 3) return false;
        if (type == 1) {
            switch (tag) {
                case 0: if (!n || v > 65535u) return false; g.page = v; break;
                case 1: if (!n) return false; g.minimum = sv; g.minimum_set = true; break;
                case 2:
                    if (!n || (g.minimum >= 0 && v > INT32_MAX)) return false;
                    g.maximum = g.minimum < 0 ? sv : (int32_t)v;
                    g.maximum_set = true; break;
                case 3: case 4: case 5: case 6: break; /* physical units unused */
                case 7: if (!n) return false; g.size = v; break;
                case 8:
                    if (!n || !v || v > 255u) return false;
                    g.id = v; ids = true; break;
                case 9: if (!n) return false; g.count = v; break;
                case 10:
                    if (n || sp == MOUSE_GLOBAL_STACK) return false;
                    stack[sp++] = g; break;
                case 11:
                    if (n || !sp) return false;
                    g = stack[--sp]; break;
                default: return false;
            }
            continue;
        }
        if (type == 2) {
            uint32_t usage;
            if (!n || !usage_value(g.page, v, n, &usage)) return false;
            if (tag == 0) {
                if (lo_set || hi_set || used == MOUSE_USAGES) return false;
                usages[used++] = usage;
            } else if (tag == 1) {
                if (used || lo_set) return false;
                lo = usage; lo_set = true;
            } else if (tag == 2) {
                if (used || hi_set) return false;
                hi = usage; hi_set = true;
            } else return false; /* delimiters/designators/string locals unsupported */
            continue;
        }
        if (lo_set != hi_set || (lo_set && (lo > hi || (lo >> 16u) != (hi >> 16u))))
            return false;
        if (tag == 10) {
            if (n != 1 || depth == MOUSE_COLLECTION_DEPTH ||
                (used ? used != 1 : (!lo_set || lo != hi))) return false;
            uint32_t usage = used ? usages[0] : lo;
            if (v == 1 && usage == 0x10002u) {
                if (depth || ++mouse_apps != 1) return false;
                mouse_depth = depth + 1u;
            } else if (mouse_depth && v == 1) return false;
            if (v > 2u) return false;
            ++depth;
        } else if (tag == 12) {
            if (n || !depth) return false;
            if (depth == mouse_depth) mouse_depth = 0;
            --depth;
        } else if (tag == 8 || tag == 9 || tag == 11) {
            if (!n || !depth || !g.size || g.size > 32u || !g.count || g.count > 64u ||
                g.size * g.count > (RISC_USB_HID_MAX_REPORT - (g.id ? 1u : 0u)) * 8u)
                return false;
            if (tag == 8) {
                const uint32_t bit = positions[g.id], bits = g.size * g.count;
                if (bit + bits > (RISC_USB_HID_MAX_REPORT - (g.id ? 1u : 0u)) * 8u) return false;
                positions[g.id] = (uint16_t)(bit + bits);
                if (mouse_depth && !(v & 1u)) {
                    if (!(v & 2u) || (v & ~7u) || !g.minimum_set || !g.maximum_set ||
                        g.minimum > g.maximum || g.size > 16u ||
                        (used ? used != g.count : (!lo_set || hi - lo + 1u != g.count)))
                        return false;
                    if (chosen && g.id != layout.report_id) return false;
                    layout.report_id = (uint8_t)g.id; chosen = true;
                    for (uint32_t i = 0; i < g.count; ++i) {
                        const uint32_t usage = used ? usages[i] : lo + i;
                        uint8_t kind = 0, index = 0;
                        if ((usage >> 16u) == 9u && (usage & 65535u) >= 1u &&
                            (usage & 65535u) <= 32u) {
                            index = (uint8_t)((usage & 65535u) - 1u); kind = 5;
                            if (v != 2 || g.size != 1 || g.minimum != 0 || g.maximum != 1 ||
                                (button_mask & (1u << index))) return false;
                            button_mask |= 1u << index;
                        } else {
                            if (usage == 0x10030u) kind = 1;
                            else if (usage == 0x10031u) kind = 2;
                            else if (usage == 0x10038u) kind = 3;
                            else if (usage == 0xc0238u) kind = 4;
                            if (!kind || v != 6 || g.minimum >= 0 || g.maximum <= 0 ||
                                g.minimum < -(int32_t)(1u << (g.size - 1u)) ||
                                g.maximum > (int32_t)((1u << (g.size - 1u)) - 1u) ||
                                (axis_mask & (1u << (kind - 1u)))) return false;
                            axis_mask |= (uint8_t)(1u << (kind - 1u));
                        }
                        if (layout.field_count == MOUSE_FIELDS) return false;
                        layout.fields[layout.field_count++] = (mouse_field){
                            (uint16_t)(bit + i * g.size), (uint8_t)g.size, kind,
                            g.minimum, g.maximum, index};
                    }
                }
            }
        } else return false;
        used = 0; lo_set = hi_set = false;
    }
    if (depth || sp || used || lo_set || hi_set || mouse_apps != 1 || !chosen ||
        (axis_mask & 3u) != 3u || (ids && positions[0])) return false;
    for (unsigned i = 0; i < 256; ++i) if (positions[i]) {
        if (layout.report_count == MOUSE_REPORT_IDS) return false;
        layout.reports[layout.report_count++] = (mouse_report){(uint8_t)i,
            (uint8_t)((positions[i] + 7u) / 8u + (i ? 1u : 0u))};
    }
    *out = layout;
    return true;
}
static int32_t extract_field(const uint8_t *data, const mouse_field *f) {
    uint32_t v = 0;
    for (unsigned i = 0; i < f->size; ++i) {
        const unsigned bit = f->bit + i;
        if (data[bit / 8u] & (1u << (bit % 8u))) v |= 1u << i;
    }
    if (f->minimum < 0 && (v & (1u << (f->size - 1u))))
        return (int32_t)v - (int32_t)(1u << f->size);
    return (int32_t)v;
}
