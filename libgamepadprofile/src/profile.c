/*
 * profile.c -- Load and validate gamepad profiles from JSON.
 */

#include "profile.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERR(fmt, ...) do { if (err_buf && err_buf_size > 0) snprintf(err_buf, err_buf_size, fmt, ##__VA_ARGS__); } while (0)

static bool parse_hex_u16(const char *s, uint16_t *out)
{
    if (!s) return false;
    if (s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return false;
    char *end = NULL;
    unsigned long v = strtoul(s + 2, &end, 16);
    if (end == s + 2 || *end != '\0' || v > 0xFFFF) return false;
    *out = (uint16_t)v;
    return true;
}

static bool parse_transport(const char *s, transport_t *out)
{
    if (!s) return false;
    if (strcmp(s, "usb") == 0)       { *out = TRANSPORT_USB; return true; }
    if (strcmp(s, "bluetooth") == 0) { *out = TRANSPORT_BLUETOOTH; return true; }
    if (strcmp(s, "any") == 0)       { *out = TRANSPORT_ANY; return true; }
    return false;
}

static bool parse_axis_type(const char *s, axis_type_t *out)
{
    if (!s) return false;
    if (strcmp(s, "u8") == 0)         { *out = AXIS_U8;         return true; }
    if (strcmp(s, "u16le") == 0)      { *out = AXIS_U16LE;      return true; }
    if (strcmp(s, "i16le") == 0)      { *out = AXIS_I16LE;      return true; }
    if (strcmp(s, "bit") == 0)        { *out = AXIS_BIT;        return true; }
    if (strcmp(s, "u12le_low") == 0)  { *out = AXIS_U12LE_LOW;  return true; }
    if (strcmp(s, "u12le_high") == 0) { *out = AXIS_U12LE_HIGH; return true; }
    return false;
}

static bool parse_dpad_encoding(const char *s, dpad_encoding_t *out)
{
    if (!s) return false;
    if (strcmp(s, "8direction") == 0)      { *out = DPAD_8DIRECTION;      return true; }
    if (strcmp(s, "individual_bits") == 0) { *out = DPAD_INDIVIDUAL_BITS; return true; }
    if (strcmp(s, "8direction_ccw") == 0)  { *out = DPAD_8DIRECTION_CCW;  return true; }
    if (strcmp(s, "bits") == 0)            { *out = DPAD_BITS;            return true; }
    return false;
}

static bool parse_output_protocol(const char *s, output_protocol_t *out)
{
    if (!s) return false;
    if (strcmp(s, "switchpro_rumble") == 0) { *out = OUTPUT_PROTO_SWITCHPRO_RUMBLE; return true; }
    return false;
}

static bool validate_byte_in_range(const char *fieldname, int byte_idx, int report_length, char *err_buf, size_t err_buf_size)
{
    if (byte_idx < 0 || byte_idx >= report_length) {
        ERR("%s: byte index %d out of range (length=%d)", fieldname, byte_idx, report_length);
        return false;
    }
    return true;
}

static bool parse_button(const cJSON *obj, button_map_t *out)
{
    if (!cJSON_IsObject(obj)) return false;
    const cJSON *byte = cJSON_GetObjectItemCaseSensitive(obj, "byte");
    const cJSON *bit  = cJSON_GetObjectItemCaseSensitive(obj, "bit");
    if (!cJSON_IsNumber(byte) || !cJSON_IsNumber(bit)) return false;
    out->byte    = byte->valueint;
    out->bit     = bit->valueint;
    out->present = true;
    return true;
}

static bool parse_stick(const cJSON *obj, stick_map_t *out)
{
    if (!cJSON_IsObject(obj)) return false;
    const cJSON *byte    = cJSON_GetObjectItemCaseSensitive(obj, "byte");
    const cJSON *type    = cJSON_GetObjectItemCaseSensitive(obj, "type");
    const cJSON *center  = cJSON_GetObjectItemCaseSensitive(obj, "center");
    const cJSON *invert  = cJSON_GetObjectItemCaseSensitive(obj, "invert");
    if (!cJSON_IsNumber(byte) || !cJSON_IsString(type)) return false;
    if (!parse_axis_type(type->valuestring, &out->type)) return false;
    out->byte    = byte->valueint;
    out->center  = cJSON_IsNumber(center) ? center->valueint : -1;
    out->invert  = cJSON_IsBool(invert) ? cJSON_IsTrue(invert) : false;
    out->present = true;
    return true;
}

static bool parse_trigger(const cJSON *obj, trigger_map_t *out)
{
    if (!cJSON_IsObject(obj)) return false;
    const cJSON *byte = cJSON_GetObjectItemCaseSensitive(obj, "byte");
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(obj, "type");
    const cJSON *bit  = cJSON_GetObjectItemCaseSensitive(obj, "bit");
    if (!cJSON_IsNumber(byte) || !cJSON_IsString(type)) return false;
    if (!parse_axis_type(type->valuestring, &out->type)) return false;
    out->byte = byte->valueint;
    if (out->type == AXIS_BIT) {
        if (!cJSON_IsNumber(bit)) return false;
        out->bit = bit->valueint;
    }
    out->present = true;
    return true;
}

profile_t *profile_load_from_string(const char *json, char *err_buf, size_t err_buf_size)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) { ERR("JSON parse error near offset %ld", (long)(cJSON_GetErrorPtr() - json)); return NULL; }

    profile_t *p = calloc(1, sizeof(*p));
    if (!p) { ERR("out of memory"); cJSON_Delete(root); return NULL; }

    /* schema_version */
    const cJSON *sv = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    if (!cJSON_IsNumber(sv)) { ERR("schema_version: missing or not a number"); goto fail; }
    p->schema_version = sv->valueint;

    /* name */
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (!cJSON_IsString(name)) { ERR("name: missing or not a string"); goto fail; }
    p->name = _strdup(name->valuestring);

    /* match { vid, pid, transport } */
    const cJSON *match = cJSON_GetObjectItemCaseSensitive(root, "match");
    if (!cJSON_IsObject(match)) { ERR("match: missing or not an object"); goto fail; }
    const cJSON *vid = cJSON_GetObjectItemCaseSensitive(match, "vid");
    const cJSON *pid = cJSON_GetObjectItemCaseSensitive(match, "pid");
    const cJSON *tr  = cJSON_GetObjectItemCaseSensitive(match, "transport");
    if (!cJSON_IsString(vid) || !parse_hex_u16(vid->valuestring, &p->vid)) { ERR("match.vid: must be hex string like \"0x3537\""); goto fail; }
    if (!cJSON_IsString(pid) || !parse_hex_u16(pid->valuestring, &p->pid)) { ERR("match.pid: must be hex string like \"0x1046\""); goto fail; }
    if (!cJSON_IsString(tr)  || !parse_transport(tr->valuestring, &p->transport)) { ERR("match.transport: must be \"usb\" | \"bluetooth\" | \"any\""); goto fail; }

    /* input_report */
    const cJSON *ir = cJSON_GetObjectItemCaseSensitive(root, "input_report");
    if (!cJSON_IsObject(ir)) { ERR("input_report: missing"); goto fail; }
    const cJSON *rid = cJSON_GetObjectItemCaseSensitive(ir, "report_id");
    const cJSON *len = cJSON_GetObjectItemCaseSensitive(ir, "length");
    if (!cJSON_IsNumber(rid)) { ERR("input_report.report_id: missing"); goto fail; }
    if (!cJSON_IsNumber(len)) { ERR("input_report.length: missing"); goto fail; }
    p->report_id     = (uint8_t)rid->valueint;
    p->report_length = len->valueint;

    /* dpad */
    const cJSON *dpad = cJSON_GetObjectItemCaseSensitive(ir, "dpad");
    if (!cJSON_IsObject(dpad)) { ERR("input_report.dpad: missing"); goto fail; }
    const cJSON *db = cJSON_GetObjectItemCaseSensitive(dpad, "byte");
    const cJSON *de = cJSON_GetObjectItemCaseSensitive(dpad, "encoding");
    if (!cJSON_IsString(de)) { ERR("input_report.dpad: encoding required"); goto fail; }
    if (!parse_dpad_encoding(de->valuestring, &p->dpad.encoding)) {
        ERR("input_report.dpad.encoding: must be \"8direction\", \"8direction_ccw\", \"individual_bits\", or \"bits\"");
        goto fail;
    }
    if (p->dpad.encoding == DPAD_BITS) {
        /* DPAD_BITS: no top-level byte needed; each direction has its own byte+bit */
        const cJSON *up    = cJSON_GetObjectItemCaseSensitive(dpad, "up");
        const cJSON *down  = cJSON_GetObjectItemCaseSensitive(dpad, "down");
        const cJSON *left  = cJSON_GetObjectItemCaseSensitive(dpad, "left");
        const cJSON *right = cJSON_GetObjectItemCaseSensitive(dpad, "right");
        if (!parse_button(up,    &p->dpad.up)    ||
            !parse_button(down,  &p->dpad.down)  ||
            !parse_button(left,  &p->dpad.left)  ||
            !parse_button(right, &p->dpad.right)) {
            ERR("input_report.dpad: bits encoding requires up/down/left/right each with byte+bit"); goto fail;
        }
        p->dpad.byte = 0; /* unused for DPAD_BITS */
    } else {
        /* 8direction / 8direction_ccw / individual_bits: top-level byte required */
        if (!cJSON_IsNumber(db)) { ERR("input_report.dpad: byte required for this encoding"); goto fail; }
        p->dpad.byte = db->valueint;
        if (p->dpad.encoding == DPAD_INDIVIDUAL_BITS) {
            const cJSON *up    = cJSON_GetObjectItemCaseSensitive(dpad, "up");
            const cJSON *down  = cJSON_GetObjectItemCaseSensitive(dpad, "down");
            const cJSON *left  = cJSON_GetObjectItemCaseSensitive(dpad, "left");
            const cJSON *right = cJSON_GetObjectItemCaseSensitive(dpad, "right");
            if (!parse_button(up,    &p->dpad.up)    ||
                !parse_button(down,  &p->dpad.down)  ||
                !parse_button(left,  &p->dpad.left)  ||
                !parse_button(right, &p->dpad.right)) {
                ERR("input_report.dpad: individual_bits requires up/down/left/right"); goto fail;
            }
        }
    }

    /* buttons */
    const cJSON *btns = cJSON_GetObjectItemCaseSensitive(ir, "buttons");
    if (!cJSON_IsObject(btns)) { ERR("input_report.buttons: missing"); goto fail; }
    /* Optional buttons: missing is silently OK; failing to parse a present one is fatal. */
    #define MAYBE_BTN(name, field) do { \
        const cJSON *o = cJSON_GetObjectItemCaseSensitive(btns, name); \
        if (o && !parse_button(o, &p->field)) { ERR("input_report.buttons." name ": invalid"); goto fail; } \
    } while (0)
    MAYBE_BTN("a", a); MAYBE_BTN("b", b); MAYBE_BTN("x", x); MAYBE_BTN("y", y);
    MAYBE_BTN("lb", lb); MAYBE_BTN("rb", rb);
    MAYBE_BTN("ls", ls); MAYBE_BTN("rs", rs);
    MAYBE_BTN("back", back); MAYBE_BTN("start", start); MAYBE_BTN("guide", guide);
    #undef MAYBE_BTN

    /* sticks */
    const cJSON *sticks = cJSON_GetObjectItemCaseSensitive(ir, "sticks");
    if (cJSON_IsObject(sticks)) {
        #define MAYBE_STICK(name, field) do { \
            const cJSON *o = cJSON_GetObjectItemCaseSensitive(sticks, name); \
            if (o && !parse_stick(o, &p->field)) { ERR("input_report.sticks." name ": invalid"); goto fail; } \
        } while (0)
        MAYBE_STICK("lx", lx); MAYBE_STICK("ly", ly); MAYBE_STICK("rx", rx); MAYBE_STICK("ry", ry);
        #undef MAYBE_STICK
    }

    /* triggers */
    const cJSON *triggers = cJSON_GetObjectItemCaseSensitive(ir, "triggers");
    if (cJSON_IsObject(triggers)) {
        #define MAYBE_TRIG(name, field) do { \
            const cJSON *o = cJSON_GetObjectItemCaseSensitive(triggers, name); \
            if (o && !parse_trigger(o, &p->field)) { ERR("input_report.triggers." name ": invalid"); goto fail; } \
        } while (0)
        MAYBE_TRIG("lt", lt); MAYBE_TRIG("rt", rt);
        #undef MAYBE_TRIG
    }

    /* output_report (optional, inside input_report section) */
    const cJSON *outr = cJSON_GetObjectItemCaseSensitive(ir, "output_report");
    if (cJSON_IsObject(outr)) {
        const cJSON *orid  = cJSON_GetObjectItemCaseSensitive(outr, "report_id");
        const cJSON *oproto = cJSON_GetObjectItemCaseSensitive(outr, "protocol");
        if (!cJSON_IsNumber(orid)) { ERR("input_report.output_report.report_id: missing or not a number"); goto fail; }
        if (orid->valueint < 0 || orid->valueint > 255) { ERR("input_report.output_report.report_id: must be 0..255"); goto fail; }
        if (!cJSON_IsString(oproto)) { ERR("input_report.output_report.protocol: missing or not a string"); goto fail; }
        if (!parse_output_protocol(oproto->valuestring, &p->output_report.protocol)) {
            ERR("input_report.output_report.protocol: unrecognized value \"%s\"", oproto->valuestring);
            goto fail;
        }
        p->output_report.report_id = orid->valueint;
        p->output_report.present   = true;
    }

    /* === Validation pass === */

    /* Rule 1: schema_version must be 1. */
    if (p->schema_version != 1) { ERR("schema_version: expected 1, got %d", p->schema_version); goto fail; }

    /* Rule 3: required elements present. */
    #define REQUIRE(field, what) do { if (!p->field.present) { ERR("missing required: " what); goto fail; } } while (0)
    REQUIRE(a, "buttons.a"); REQUIRE(b, "buttons.b"); REQUIRE(x, "buttons.x"); REQUIRE(y, "buttons.y");
    REQUIRE(lb, "buttons.lb"); REQUIRE(rb, "buttons.rb");
    REQUIRE(start, "buttons.start"); REQUIRE(back, "buttons.back");
    REQUIRE(lt, "triggers.lt"); REQUIRE(rt, "triggers.rt");
    #undef REQUIRE

    /* Rule 2: every referenced byte index < length. */
    if (p->dpad.encoding == DPAD_BITS || p->dpad.encoding == DPAD_INDIVIDUAL_BITS) {
        if (!validate_byte_in_range("dpad.up.byte",    p->dpad.up.byte,    p->report_length, err_buf, err_buf_size)) goto fail;
        if (!validate_byte_in_range("dpad.down.byte",  p->dpad.down.byte,  p->report_length, err_buf, err_buf_size)) goto fail;
        if (!validate_byte_in_range("dpad.left.byte",  p->dpad.left.byte,  p->report_length, err_buf, err_buf_size)) goto fail;
        if (!validate_byte_in_range("dpad.right.byte", p->dpad.right.byte, p->report_length, err_buf, err_buf_size)) goto fail;
    } else {
        if (!validate_byte_in_range("dpad.byte", p->dpad.byte, p->report_length, err_buf, err_buf_size)) goto fail;
    }
    #define CHECK_BTN(field, name) do { if (p->field.present && !validate_byte_in_range("buttons." name ".byte", p->field.byte, p->report_length, err_buf, err_buf_size)) goto fail; } while (0)
    CHECK_BTN(a, "a"); CHECK_BTN(b, "b"); CHECK_BTN(x, "x"); CHECK_BTN(y, "y");
    CHECK_BTN(lb, "lb"); CHECK_BTN(rb, "rb");
    CHECK_BTN(ls, "ls"); CHECK_BTN(rs, "rs");
    CHECK_BTN(start, "start"); CHECK_BTN(back, "back"); CHECK_BTN(guide, "guide");
    #undef CHECK_BTN
    /* For sticks/triggers we may need 2 bytes (u16le/i16le/u12le); check both. */
    #define AXIS_NEEDS_BYTE1(t) ((t) == AXIS_U16LE || (t) == AXIS_I16LE || (t) == AXIS_U12LE_LOW || (t) == AXIS_U12LE_HIGH)
    #define CHECK_AXIS(field, name) do { \
        if (p->field.present) { \
            if (!validate_byte_in_range("sticks." name ".byte", p->field.byte, p->report_length, err_buf, err_buf_size)) goto fail; \
            if (AXIS_NEEDS_BYTE1(p->field.type) && !validate_byte_in_range("sticks." name ".byte+1", p->field.byte + 1, p->report_length, err_buf, err_buf_size)) goto fail; \
        } \
    } while (0)
    CHECK_AXIS(lx, "lx");
    CHECK_AXIS(ly, "ly");
    CHECK_AXIS(rx, "rx");
    CHECK_AXIS(ry, "ry");
    #undef CHECK_AXIS
    #undef AXIS_NEEDS_BYTE1
    #define CHECK_TRIG(field, name) do { \
        if (p->field.present) { \
            if (!validate_byte_in_range("triggers." name ".byte", p->field.byte, p->report_length, err_buf, err_buf_size)) goto fail; \
            if (p->field.type == AXIS_U16LE && !validate_byte_in_range("triggers." name ".byte+1", p->field.byte + 1, p->report_length, err_buf, err_buf_size)) goto fail; \
        } \
    } while (0)
    CHECK_TRIG(lt, "lt");
    CHECK_TRIG(rt, "rt");
    #undef CHECK_TRIG

    cJSON_Delete(root);
    return p;

fail:
    cJSON_Delete(root);
    profile_free(p);
    return NULL;
}

profile_t *profile_load_from_file(const char *path, char *err_buf, size_t err_buf_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { ERR("could not open %s", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 1024 * 1024) { ERR("file size out of range: %ld", sz); fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { ERR("out of memory"); fclose(f); return NULL; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    profile_t *p = profile_load_from_string(buf, err_buf, err_buf_size);
    free(buf);
    return p;
}

void profile_free(profile_t *profile)
{
    if (!profile) return;
    free(profile->name);
    free(profile);
}
