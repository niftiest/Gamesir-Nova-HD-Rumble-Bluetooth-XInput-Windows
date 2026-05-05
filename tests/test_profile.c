/*
 * test_profile.c -- Unit tests for profile_load_from_string + profile_load_from_file.
 */

#include "profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *VALID_PROFILE =
"{"
"  \"schema_version\": 1,"
"  \"name\": \"Test Pad\","
"  \"match\": { \"vid\": \"0x3537\", \"pid\": \"0x1046\", \"transport\": \"bluetooth\" },"
"  \"input_report\": {"
"    \"report_id\": 1,"
"    \"length\": 16,"
"    \"dpad\": { \"byte\": 1, \"encoding\": \"8direction\" },"
"    \"buttons\": {"
"      \"a\":     { \"byte\": 3, \"bit\": 0 },"
"      \"b\":     { \"byte\": 3, \"bit\": 1 },"
"      \"x\":     { \"byte\": 3, \"bit\": 2 },"
"      \"y\":     { \"byte\": 3, \"bit\": 3 },"
"      \"lb\":    { \"byte\": 3, \"bit\": 4 },"
"      \"rb\":    { \"byte\": 3, \"bit\": 5 },"
"      \"back\":  { \"byte\": 2, \"bit\": 0 },"
"      \"start\": { \"byte\": 2, \"bit\": 1 }"
"    },"
"    \"sticks\": {"
"      \"lx\": { \"byte\": 4, \"type\": \"u8\", \"center\": 128, \"invert\": false },"
"      \"ly\": { \"byte\": 5, \"type\": \"u8\", \"center\": 128, \"invert\": true  }"
"    },"
"    \"triggers\": {"
"      \"lt\": { \"byte\": 10, \"type\": \"u8\" },"
"      \"rt\": { \"byte\": 11, \"type\": \"u8\" }"
"    }"
"  }"
"}";

static void test_load_valid(void)
{
    char err[PROFILE_ERR_LEN] = {0};
    profile_t *p = profile_load_from_string(VALID_PROFILE, err, sizeof(err));
    assert(p != NULL);
    assert(p->schema_version == 1);
    assert(strcmp(p->name, "Test Pad") == 0);
    assert(p->vid == 0x3537);
    assert(p->pid == 0x1046);
    assert(p->transport == TRANSPORT_BLUETOOTH);
    assert(p->report_id == 1);
    assert(p->report_length == 16);
    assert(p->dpad.byte == 1);
    assert(p->dpad.encoding == DPAD_8DIRECTION);
    assert(p->a.present && p->a.byte == 3 && p->a.bit == 0);
    assert(p->y.present && p->y.byte == 3 && p->y.bit == 3);
    assert(p->start.present);
    assert(p->back.present);
    assert(!p->guide.present);  /* not in JSON */
    assert(!p->ls.present);     /* not in JSON */
    assert(p->lx.present && p->lx.type == AXIS_U8 && p->lx.center == 128 && !p->lx.invert);
    assert(p->ly.present && p->ly.invert);
    assert(p->lt.present && p->lt.type == AXIS_U8);
    profile_free(p);
    printf("PASS test_load_valid\n");
}

/* Wrong schema_version → fatal. */
static void test_reject_wrong_schema_version(void)
{
    const char *bad = "{ \"schema_version\": 2, \"name\": \"x\", \"match\": {\"vid\":\"0x1\",\"pid\":\"0x2\",\"transport\":\"any\"}, \"input_report\": {\"report_id\":1,\"length\":4,\"dpad\":{\"byte\":0,\"encoding\":\"8direction\"},\"buttons\":{\"a\":{\"byte\":0,\"bit\":0},\"b\":{\"byte\":0,\"bit\":0},\"x\":{\"byte\":0,\"bit\":0},\"y\":{\"byte\":0,\"bit\":0},\"lb\":{\"byte\":0,\"bit\":0},\"rb\":{\"byte\":0,\"bit\":0},\"start\":{\"byte\":0,\"bit\":0},\"back\":{\"byte\":0,\"bit\":0}},\"triggers\":{\"lt\":{\"byte\":0,\"type\":\"u8\"},\"rt\":{\"byte\":0,\"type\":\"u8\"}}} }";
    char err[PROFILE_ERR_LEN] = {0};
    profile_t *p = profile_load_from_string(bad, err, sizeof(err));
    assert(p == NULL);
    assert(strstr(err, "schema_version") != NULL);
    printf("PASS test_reject_wrong_schema_version\n");
}

/* Missing required button (e.g. A) → fatal. */
static void test_reject_missing_button_a(void)
{
    const char *bad = "{ \"schema_version\": 1, \"name\": \"x\", \"match\": {\"vid\":\"0x1\",\"pid\":\"0x2\",\"transport\":\"any\"}, \"input_report\": {\"report_id\":1,\"length\":4,\"dpad\":{\"byte\":0,\"encoding\":\"8direction\"},\"buttons\":{\"b\":{\"byte\":0,\"bit\":0},\"x\":{\"byte\":0,\"bit\":0},\"y\":{\"byte\":0,\"bit\":0},\"lb\":{\"byte\":0,\"bit\":0},\"rb\":{\"byte\":0,\"bit\":0},\"start\":{\"byte\":0,\"bit\":0},\"back\":{\"byte\":0,\"bit\":0}},\"triggers\":{\"lt\":{\"byte\":0,\"type\":\"u8\"},\"rt\":{\"byte\":0,\"type\":\"u8\"}}} }";
    char err[PROFILE_ERR_LEN] = {0};
    profile_t *p = profile_load_from_string(bad, err, sizeof(err));
    assert(p == NULL);
    assert(strstr(err, "a") != NULL);
    printf("PASS test_reject_missing_button_a\n");
}

/* Byte out of range (>= length) → fatal. */
static void test_reject_byte_out_of_range(void)
{
    const char *bad = "{ \"schema_version\": 1, \"name\": \"x\", \"match\": {\"vid\":\"0x1\",\"pid\":\"0x2\",\"transport\":\"any\"}, \"input_report\": {\"report_id\":1,\"length\":4,\"dpad\":{\"byte\":0,\"encoding\":\"8direction\"},\"buttons\":{\"a\":{\"byte\":99,\"bit\":0},\"b\":{\"byte\":0,\"bit\":0},\"x\":{\"byte\":0,\"bit\":0},\"y\":{\"byte\":0,\"bit\":0},\"lb\":{\"byte\":0,\"bit\":0},\"rb\":{\"byte\":0,\"bit\":0},\"start\":{\"byte\":0,\"bit\":0},\"back\":{\"byte\":0,\"bit\":0}},\"triggers\":{\"lt\":{\"byte\":0,\"type\":\"u8\"},\"rt\":{\"byte\":0,\"type\":\"u8\"}}} }";
    char err[PROFILE_ERR_LEN] = {0};
    profile_t *p = profile_load_from_string(bad, err, sizeof(err));
    assert(p == NULL);
    assert(strstr(err, "byte") != NULL || strstr(err, "range") != NULL);
    printf("PASS test_reject_byte_out_of_range\n");
}

/* Unknown top-level keys are tolerated. */
static void test_unknown_keys_tolerated(void)
{
    /* Same as VALID_PROFILE but with an extra unknown top-level key */
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", VALID_PROFILE);
    /* Splice in an unknown key at the start */
    const char *withExtra = "{ \"future_thing\": 42, \"schema_version\": 1, \"name\": \"x\", \"match\": {\"vid\":\"0x1\",\"pid\":\"0x2\",\"transport\":\"any\"}, \"input_report\": {\"report_id\":1,\"length\":4,\"dpad\":{\"byte\":0,\"encoding\":\"8direction\"},\"buttons\":{\"a\":{\"byte\":0,\"bit\":0},\"b\":{\"byte\":0,\"bit\":0},\"x\":{\"byte\":0,\"bit\":0},\"y\":{\"byte\":0,\"bit\":0},\"lb\":{\"byte\":0,\"bit\":0},\"rb\":{\"byte\":0,\"bit\":0},\"start\":{\"byte\":0,\"bit\":0},\"back\":{\"byte\":0,\"bit\":0}},\"triggers\":{\"lt\":{\"byte\":0,\"type\":\"u8\"},\"rt\":{\"byte\":0,\"type\":\"u8\"}}} }";
    char err[PROFILE_ERR_LEN] = {0};
    profile_t *p = profile_load_from_string(withExtra, err, sizeof(err));
    assert(p != NULL);
    profile_free(p);
    printf("PASS test_unknown_keys_tolerated\n");
}

static const char *VALID_PROFILE_BIT_TRIGGERS =
"{"
"  \"schema_version\": 1,"
"  \"name\": \"BitTrig Pad\","
"  \"match\": { \"vid\": \"0x3537\", \"pid\": \"0x1046\", \"transport\": \"bluetooth\" },"
"  \"input_report\": {"
"    \"report_id\": 63,"
"    \"length\": 12,"
"    \"dpad\": { \"byte\": 3, \"encoding\": \"8direction\" },"
"    \"buttons\": {"
"      \"a\":     { \"byte\": 1, \"bit\": 1 },"
"      \"b\":     { \"byte\": 1, \"bit\": 0 },"
"      \"x\":     { \"byte\": 1, \"bit\": 4 },"
"      \"y\":     { \"byte\": 1, \"bit\": 3 },"
"      \"lb\":    { \"byte\": 1, \"bit\": 6 },"
"      \"rb\":    { \"byte\": 1, \"bit\": 7 },"
"      \"back\":  { \"byte\": 2, \"bit\": 2 },"
"      \"start\": { \"byte\": 2, \"bit\": 4 }"
"    },"
"    \"triggers\": {"
"      \"lt\": { \"byte\": 2, \"bit\": 0, \"type\": \"bit\" },"
"      \"rt\": { \"byte\": 2, \"bit\": 1, \"type\": \"bit\" }"
"    }"
"  }"
"}";

static void test_load_with_bit_triggers(void)
{
    char err[PROFILE_ERR_LEN] = {0};
    profile_t *p = profile_load_from_string(VALID_PROFILE_BIT_TRIGGERS, err, sizeof(err));
    assert(p != NULL);
    assert(p->lt.present && p->lt.type == AXIS_BIT && p->lt.byte == 2 && p->lt.bit == 0);
    assert(p->rt.present && p->rt.type == AXIS_BIT && p->rt.byte == 2 && p->rt.bit == 1);
    profile_free(p);
    printf("PASS test_load_with_bit_triggers\n");
}

/* Profile with 8direction_ccw dpad encoding. */
static const char *VALID_PROFILE_CCW_DPAD =
"{"
"  \"schema_version\": 1,"
"  \"name\": \"CCW Dpad Pad\","
"  \"match\": { \"vid\": \"0x057E\", \"pid\": \"0x2009\", \"transport\": \"bluetooth\" },"
"  \"input_report\": {"
"    \"report_id\": 63,"
"    \"length\": 12,"
"    \"dpad\": { \"byte\": 3, \"encoding\": \"8direction_ccw\" },"
"    \"buttons\": {"
"      \"a\":     { \"byte\": 1, \"bit\": 0 },"
"      \"b\":     { \"byte\": 1, \"bit\": 1 },"
"      \"x\":     { \"byte\": 1, \"bit\": 2 },"
"      \"y\":     { \"byte\": 1, \"bit\": 3 },"
"      \"lb\":    { \"byte\": 1, \"bit\": 4 },"
"      \"rb\":    { \"byte\": 1, \"bit\": 5 },"
"      \"back\":  { \"byte\": 2, \"bit\": 0 },"
"      \"start\": { \"byte\": 2, \"bit\": 1 }"
"    },"
"    \"triggers\": {"
"      \"lt\": { \"byte\": 1, \"bit\": 6, \"type\": \"bit\" },"
"      \"rt\": { \"byte\": 1, \"bit\": 7, \"type\": \"bit\" }"
"    }"
"  }"
"}";

static void test_load_8direction_ccw(void)
{
    char err[PROFILE_ERR_LEN] = {0};
    profile_t *p = profile_load_from_string(VALID_PROFILE_CCW_DPAD, err, sizeof(err));
    assert(p != NULL);
    assert(p->dpad.byte == 3);
    assert(p->dpad.encoding == DPAD_8DIRECTION_CCW);
    assert(!p->output_report.present);
    profile_free(p);
    printf("PASS test_load_8direction_ccw\n");
}

/* Profile with output_report section (switchpro_rumble). */
static const char *VALID_PROFILE_WITH_OUTPUT =
"{"
"  \"schema_version\": 1,"
"  \"name\": \"Gamesir Nova HD Rumble (Pro Controller mode, Bluetooth)\","
"  \"match\": { \"vid\": \"0x057E\", \"pid\": \"0x2009\", \"transport\": \"bluetooth\" },"
"  \"input_report\": {"
"    \"report_id\": 63,"
"    \"length\": 12,"
"    \"dpad\": { \"byte\": 3, \"encoding\": \"8direction_ccw\" },"
"    \"buttons\": {"
"      \"a\":     { \"byte\": 1, \"bit\": 0 },"
"      \"b\":     { \"byte\": 1, \"bit\": 1 },"
"      \"x\":     { \"byte\": 1, \"bit\": 2 },"
"      \"y\":     { \"byte\": 1, \"bit\": 3 },"
"      \"lb\":    { \"byte\": 1, \"bit\": 4 },"
"      \"rb\":    { \"byte\": 1, \"bit\": 5 },"
"      \"back\":  { \"byte\": 2, \"bit\": 0 },"
"      \"start\": { \"byte\": 2, \"bit\": 1 }"
"    },"
"    \"triggers\": {"
"      \"lt\": { \"byte\": 1, \"bit\": 6, \"type\": \"bit\" },"
"      \"rt\": { \"byte\": 1, \"bit\": 7, \"type\": \"bit\" }"
"    },"
"    \"output_report\": {"
"      \"report_id\": 16,"
"      \"protocol\":  \"switchpro_rumble\""
"    }"
"  }"
"}";

static void test_load_output_report_switchpro_rumble(void)
{
    char err[PROFILE_ERR_LEN] = {0};
    profile_t *p = profile_load_from_string(VALID_PROFILE_WITH_OUTPUT, err, sizeof(err));
    assert(p != NULL);
    assert(p->output_report.present);
    assert(p->output_report.report_id == 16);
    assert(p->output_report.protocol == OUTPUT_PROTO_SWITCHPRO_RUMBLE);
    profile_free(p);
    printf("PASS test_load_output_report_switchpro_rumble\n");
}

/* output_report with invalid report_id (> 255) should fail. */
static void test_reject_output_report_invalid_id(void)
{
    const char *bad =
        "{ \"schema_version\": 1, \"name\": \"x\","
        "  \"match\": {\"vid\":\"0x1\",\"pid\":\"0x2\",\"transport\":\"any\"},"
        "  \"input_report\": {"
        "    \"report_id\":1,\"length\":4,"
        "    \"dpad\":{\"byte\":0,\"encoding\":\"8direction\"},"
        "    \"buttons\":{\"a\":{\"byte\":0,\"bit\":0},\"b\":{\"byte\":0,\"bit\":0},"
        "                 \"x\":{\"byte\":0,\"bit\":0},\"y\":{\"byte\":0,\"bit\":0},"
        "                 \"lb\":{\"byte\":0,\"bit\":0},\"rb\":{\"byte\":0,\"bit\":0},"
        "                 \"start\":{\"byte\":0,\"bit\":0},\"back\":{\"byte\":0,\"bit\":0}},"
        "    \"triggers\":{\"lt\":{\"byte\":0,\"type\":\"u8\"},\"rt\":{\"byte\":0,\"type\":\"u8\"}},"
        "    \"output_report\":{\"report_id\":999,\"protocol\":\"switchpro_rumble\"}"
        "  }"
        "}";
    char err[PROFILE_ERR_LEN] = {0};
    profile_t *p = profile_load_from_string(bad, err, sizeof(err));
    assert(p == NULL);
    printf("PASS test_reject_output_report_invalid_id\n");
}

/* Profile with "bits" dpad encoding and u12le axis types (Switch Pro 0x30 layout). */
static const char *VALID_PROFILE_BITS_DPAD_U12 =
"{"
"  \"schema_version\": 1,"
"  \"name\": \"Gamesir Nova HD Rumble (Pro Controller mode, Bluetooth)\","
"  \"match\": { \"vid\": \"0x057E\", \"pid\": \"0x2009\", \"transport\": \"bluetooth\" },"
"  \"input_report\": {"
"    \"report_id\": 48,"
"    \"length\": 12,"
"    \"dpad\": {"
"      \"encoding\": \"bits\","
"      \"up\":    { \"byte\": 5, \"bit\": 1 },"
"      \"down\":  { \"byte\": 5, \"bit\": 0 },"
"      \"left\":  { \"byte\": 5, \"bit\": 3 },"
"      \"right\": { \"byte\": 5, \"bit\": 2 }"
"    },"
"    \"buttons\": {"
"      \"a\":     { \"byte\": 3, \"bit\": 3 },"
"      \"b\":     { \"byte\": 3, \"bit\": 2 },"
"      \"x\":     { \"byte\": 3, \"bit\": 1 },"
"      \"y\":     { \"byte\": 3, \"bit\": 0 },"
"      \"lb\":    { \"byte\": 5, \"bit\": 6 },"
"      \"rb\":    { \"byte\": 3, \"bit\": 6 },"
"      \"back\":  { \"byte\": 4, \"bit\": 0 },"
"      \"start\": { \"byte\": 4, \"bit\": 1 }"
"    },"
"    \"sticks\": {"
"      \"lx\": { \"byte\": 6,  \"type\": \"u12le_low\",  \"center\": 2048, \"invert\": false },"
"      \"ly\": { \"byte\": 7,  \"type\": \"u12le_high\", \"center\": 2048, \"invert\": true  },"
"      \"rx\": { \"byte\": 9,  \"type\": \"u12le_low\",  \"center\": 2048, \"invert\": false },"
"      \"ry\": { \"byte\": 10, \"type\": \"u12le_high\", \"center\": 2048, \"invert\": true  }"
"    },"
"    \"triggers\": {"
"      \"lt\": { \"byte\": 5, \"bit\": 7, \"type\": \"bit\" },"
"      \"rt\": { \"byte\": 3, \"bit\": 7, \"type\": \"bit\" }"
"    },"
"    \"output_report\": {"
"      \"report_id\": 16,"
"      \"protocol\":  \"switchpro_rumble\""
"    }"
"  }"
"}";

static void test_load_bits_dpad_and_u12le_sticks(void)
{
    char err[PROFILE_ERR_LEN] = {0};
    profile_t *p = profile_load_from_string(VALID_PROFILE_BITS_DPAD_U12, err, sizeof(err));
    assert(p != NULL);
    assert(p->report_id == 48);
    assert(p->dpad.encoding == DPAD_BITS);
    assert(p->dpad.up.byte    == 5 && p->dpad.up.bit    == 1);
    assert(p->dpad.down.byte  == 5 && p->dpad.down.bit  == 0);
    assert(p->dpad.left.byte  == 5 && p->dpad.left.bit  == 3);
    assert(p->dpad.right.byte == 5 && p->dpad.right.bit == 2);
    assert(p->lx.present && p->lx.type == AXIS_U12LE_LOW  && p->lx.center == 2048 && !p->lx.invert);
    assert(p->ly.present && p->ly.type == AXIS_U12LE_HIGH && p->ly.center == 2048 &&  p->ly.invert);
    assert(p->rx.present && p->rx.type == AXIS_U12LE_LOW  && p->rx.center == 2048 && !p->rx.invert);
    assert(p->ry.present && p->ry.type == AXIS_U12LE_HIGH && p->ry.center == 2048 &&  p->ry.invert);
    assert(p->lt.present && p->lt.type == AXIS_BIT && p->lt.byte == 5 && p->lt.bit == 7);
    assert(p->rt.present && p->rt.type == AXIS_BIT && p->rt.byte == 3 && p->rt.bit == 7);
    assert(p->output_report.present);
    assert(p->output_report.report_id == 16);
    assert(p->output_report.protocol == OUTPUT_PROTO_SWITCHPRO_RUMBLE);
    profile_free(p);
    printf("PASS test_load_bits_dpad_and_u12le_sticks\n");
}

/* DPAD_BITS encoding requires all 4 directions; missing one should fail. */
static void test_reject_bits_dpad_missing_direction(void)
{
    const char *bad =
        "{ \"schema_version\": 1, \"name\": \"x\","
        "  \"match\": {\"vid\":\"0x1\",\"pid\":\"0x2\",\"transport\":\"any\"},"
        "  \"input_report\": {"
        "    \"report_id\":48,\"length\":12,"
        "    \"dpad\":{"
        "      \"encoding\":\"bits\","
        "      \"up\":{\"byte\":5,\"bit\":1},"
        "      \"down\":{\"byte\":5,\"bit\":0},"
        "      \"left\":{\"byte\":5,\"bit\":3}"
        "    },"
        "    \"buttons\":{\"a\":{\"byte\":3,\"bit\":3},\"b\":{\"byte\":3,\"bit\":2},"
        "                 \"x\":{\"byte\":3,\"bit\":1},\"y\":{\"byte\":3,\"bit\":0},"
        "                 \"lb\":{\"byte\":5,\"bit\":6},\"rb\":{\"byte\":3,\"bit\":6},"
        "                 \"start\":{\"byte\":4,\"bit\":1},\"back\":{\"byte\":4,\"bit\":0}},"
        "    \"triggers\":{\"lt\":{\"byte\":5,\"bit\":7,\"type\":\"bit\"},\"rt\":{\"byte\":3,\"bit\":7,\"type\":\"bit\"}}"
        "  }"
        "}";
    char err[PROFILE_ERR_LEN] = {0};
    profile_t *p = profile_load_from_string(bad, err, sizeof(err));
    assert(p == NULL);
    printf("PASS test_reject_bits_dpad_missing_direction\n");
}

/* output_report with unknown protocol string should fail. */
static void test_reject_output_report_unknown_protocol(void)
{
    const char *bad =
        "{ \"schema_version\": 1, \"name\": \"x\","
        "  \"match\": {\"vid\":\"0x1\",\"pid\":\"0x2\",\"transport\":\"any\"},"
        "  \"input_report\": {"
        "    \"report_id\":1,\"length\":4,"
        "    \"dpad\":{\"byte\":0,\"encoding\":\"8direction\"},"
        "    \"buttons\":{\"a\":{\"byte\":0,\"bit\":0},\"b\":{\"byte\":0,\"bit\":0},"
        "                 \"x\":{\"byte\":0,\"bit\":0},\"y\":{\"byte\":0,\"bit\":0},"
        "                 \"lb\":{\"byte\":0,\"bit\":0},\"rb\":{\"byte\":0,\"bit\":0},"
        "                 \"start\":{\"byte\":0,\"bit\":0},\"back\":{\"byte\":0,\"bit\":0}},"
        "    \"triggers\":{\"lt\":{\"byte\":0,\"type\":\"u8\"},\"rt\":{\"byte\":0,\"type\":\"u8\"}},"
        "    \"output_report\":{\"report_id\":16,\"protocol\":\"unknown_proto\"}"
        "  }"
        "}";
    char err[PROFILE_ERR_LEN] = {0};
    profile_t *p = profile_load_from_string(bad, err, sizeof(err));
    assert(p == NULL);
    printf("PASS test_reject_output_report_unknown_protocol\n");
}

int main(void)
{
    test_load_valid();
    test_reject_wrong_schema_version();
    test_reject_missing_button_a();
    test_reject_byte_out_of_range();
    test_unknown_keys_tolerated();
    test_load_with_bit_triggers();
    test_load_8direction_ccw();
    test_load_output_report_switchpro_rumble();
    test_reject_output_report_invalid_id();
    test_reject_output_report_unknown_protocol();
    test_load_bits_dpad_and_u12le_sticks();
    test_reject_bits_dpad_missing_direction();
    printf("ALL test_profile PASSED\n");
    return 0;
}
