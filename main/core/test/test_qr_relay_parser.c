#include "../../qr/parser.h"
#include "../../../components/cUR/src/crc32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(name, expr)                                               \
  do {                                                                        \
    if (!(expr)) {                                                            \
      fprintf(stderr, "FAIL: %s\n", name);                                    \
      exit(1);                                                                \
    }                                                                         \
  } while (0)

#define ASSERT_EQ_INT(name, got, want)                                        \
  do {                                                                        \
    if ((got) != (want)) {                                                    \
      fprintf(stderr, "FAIL: %s got=%d want=%d\n", name, (got), (want));      \
      exit(1);                                                                \
    }                                                                         \
  } while (0)

#define ASSERT_STREQ(name, got, want)                                         \
  do {                                                                        \
    if (strcmp((got), (want)) != 0) {                                         \
      fprintf(stderr, "FAIL: %s\nGOT : %s\nWANT: %s\n", name, (got),         \
              (want));                                                        \
      exit(1);                                                                \
    }                                                                         \
  } while (0)

static void test_tpr1_relay_pages(void) {
  static const char *pages[] = {
      "tpr1:1/4.2284751381.eJxFx8EKwjAQRdGvSXaVNBYDwiyUUnAn6F5CO7ShSSYmY_19Iy7cvPMu",
      "tpr1:2/4.2284751381.p2PCXChaf3NzbLYajiK0OyVTJqaRPNxpxXilcUWWEflNeQXcghwX6-LD",
      "tpr1:3/4.2284751381.TdDKjM8XFr5MwJWmlcnyAkHooeuENtWD-vlnUHKybEGYs9A6YCl2xvrE",
      "tpr1:4/4.2284751381._lR3Qe_pW6b_AKZ_NIU",
  };
  const char *expected =
      "tp:personalSign-version=1.0&protocol=TokenPocket&network=evm&chain_id=1&"
      "requestId=test-1&path=m%2F44%27%2F60%27%2F0%27%2F0%2F0&data=%7B%"
      "22message%22%3A%22hello%22%7D";

  QRPartParser *parser = qr_parser_create();
  ASSERT_TRUE("create parser", parser != NULL);
  for (int i = 0; i < 4; i++) {
    int parsed = qr_parser_parse(parser, pages[i]);
    ASSERT_EQ_INT("tpr1 part index", parsed, i);
  }
  ASSERT_EQ_INT("tpr1 format", qr_parser_get_format(parser), FORMAT_RELAY);
  ASSERT_EQ_INT("tpr1 count", qr_parser_parsed_count(parser), 4);
  ASSERT_EQ_INT("tpr1 total", qr_parser_total_count(parser), 4);
  ASSERT_TRUE("tpr1 complete", qr_parser_is_complete(parser));

  size_t result_len = 0;
  char *result = qr_parser_result(parser, &result_len);
  ASSERT_TRUE("tpr1 result", result != NULL);
  ASSERT_EQ_INT("tpr1 result len", (int)result_len, (int)strlen(expected));
  ASSERT_STREQ("tpr1 payload", result, expected);
  free(result);
  qr_parser_destroy(parser);
}

static void test_w3r1_relay_pages(void) {
  static const char *pages[] = {
      "w3r1:1/10.2778296571.eJydUU1Pg0AQ_SuEBHppBWqlpgkHjNg0WjVS9UhWGAsp7G53t7WN8eLd",
      "w3r1:2/10.2778296571.xD9ijCdv_hs_foaz_bAejUCY3ffmzbydvTWnIGTBqNnx6uYNKUtQZsfc",
      "w3r1:3/10.2778296571.6w260cBcIwklFWi4UENQxuUyrW5eM1ERnX8Ic6kYBcMxwkJ0CTfOz5Af",
      "w3r1:4/10.2778296571.i0TNuVaCyhuyGNKGgPEEpBaTVC36mtFF3_h8ent_fPh6fv14uUeOk3nJ",
      "w3r1:5/10.2778296571.SIak4h2ODhklZaz1K7uBt-XaXDDFUlYGAzYCesrSESibgrphYhTAtLLT",
      "w3r1:6/10.2778296571.nBQ0KbLAs1d9e1mgMDQ8mxOVB5XVPGi1rGYbo-8u4yYcuHZGFAms9p7V",
      "w3r1:7/10.2778296571.bFYgJRkCrqztEP85lCXTu_Y-OhYgOaMSkrWpX4cmaiJgkbPwgIaQJFcp",
      "w3r1:8/10.2778296571.QkwUw4IuRzsC4AjpluuxnUZn8clxeJT0ozgOu9GvIj9pi3LehtAdl2wO",
      "w3r1:9/10.2778296571.M6zh7_o7forvNarXE0Hc03eQZWhc4s6deX98NrJEzxC1ldNq1RzfrTn6",
      "w3r1:10/10.2778296571.c1zMgBmHVEGW_KvD3TcCd9Bx",
  };

  QRPartParser *parser = qr_parser_create();
  ASSERT_TRUE("create parser", parser != NULL);
  for (int i = 0; i < 10; i++) {
    int parsed = qr_parser_parse(parser, pages[i]);
    ASSERT_EQ_INT("w3r1 part index", parsed, i);
  }
  ASSERT_EQ_INT("w3r1 format", qr_parser_get_format(parser), FORMAT_RELAY);
  ASSERT_EQ_INT("w3r1 count", qr_parser_parsed_count(parser), 10);
  ASSERT_EQ_INT("w3r1 total", qr_parser_total_count(parser), 10);
  ASSERT_TRUE("w3r1 complete", qr_parser_is_complete(parser));

  size_t result_len = 0;
  char *result = qr_parser_result(parser, &result_len);
  ASSERT_TRUE("w3r1 result", result != NULL);
  ASSERT_TRUE("w3r1 wallet", strstr(result, "\"wallet\":\"BITGET\"") != NULL);
  ASSERT_TRUE("w3r1 protocol",
              strstr(result, "\"response_protocol\":\"eth-signature\"") !=
                  NULL);
  ASSERT_TRUE("w3r1 payload",
              strstr(result, "\"payload\":\"tp:personalSign-") != NULL);
  ASSERT_TRUE("w3r1 len", result_len == strlen(result));
  free(result);
  qr_parser_destroy(parser);
}

static void test_tp_multifragment_pages(void) {
  static const char *pages[] = {
      "tp:multiFragment-?data=%7B%22content%22%3A%22tp%3ApersonalSign-version%3D1.0%26protocol%3DTokenPocket%26n_1401393886%22%2C%22index%22%3A%221%2F3%22%7D",
      "tp:multiFragment-?data=%7B%22content%22%3A%22etwork%3Devm%26chain_id%3D1%26requestId%3Dold%26data%3D%257B%2522mes_1401393886%22%2C%22index%22%3A%222%2F3%22%7D",
      "tp:multiFragment-?data=%7B%22content%22%3A%22sage%2522%253A%2522hello%2522%257D_1401393886%22%2C%22index%22%3A%223%2F3%22%7D",
  };
  const char *expected =
      "tp:personalSign-version=1.0&protocol=TokenPocket&network=evm&chain_id=1&"
      "requestId=old&data=%7B%22message%22%3A%22hello%22%7D";

  QRPartParser *parser = qr_parser_create();
  ASSERT_TRUE("create parser", parser != NULL);
  for (int i = 0; i < 3; i++) {
    int parsed = qr_parser_parse(parser, pages[i]);
    ASSERT_EQ_INT("tp multi part index", parsed, i);
  }
  ASSERT_EQ_INT("tp multi format", qr_parser_get_format(parser),
                FORMAT_TP_MULTI);
  ASSERT_EQ_INT("tp multi count", qr_parser_parsed_count(parser), 3);
  ASSERT_EQ_INT("tp multi total", qr_parser_total_count(parser), 3);
  ASSERT_TRUE("tp multi complete", qr_parser_is_complete(parser));

  size_t result_len = 0;
  char *result = qr_parser_result(parser, &result_len);
  ASSERT_TRUE("tp multi result", result != NULL);
  ASSERT_EQ_INT("tp multi result len", (int)result_len,
                (int)strlen(expected));
  ASSERT_STREQ("tp multi payload", result, expected);
  free(result);
  qr_parser_destroy(parser);
}

static void test_tp_multifragment_long_content(void) {
  const size_t payload_len = 1500;
  char *payload = malloc(payload_len + 1);
  ASSERT_TRUE("long tp payload alloc", payload != NULL);
  memset(payload, 'a', payload_len);
  payload[payload_len] = '\0';

  uint32_t crc = crc32_calculate((const uint8_t *)payload, payload_len);
  int needed = snprintf(
      NULL, 0,
      "tp:multiFragment-?data=%%7B%%22content%%22%%3A%%22%s_%lu"
      "%%22%%2C%%22index%%22%%3A%%221%%2F1%%22%%7D",
      payload, (unsigned long)crc);
  ASSERT_TRUE("long tp page size", needed > 0);

  char *page = malloc((size_t)needed + 1);
  ASSERT_TRUE("long tp page alloc", page != NULL);
  snprintf(page, (size_t)needed + 1,
           "tp:multiFragment-?data=%%7B%%22content%%22%%3A%%22%s_%lu"
           "%%22%%2C%%22index%%22%%3A%%221%%2F1%%22%%7D",
           payload, (unsigned long)crc);

  QRPartParser *parser = qr_parser_create();
  ASSERT_TRUE("create long tp parser", parser != NULL);
  int parsed = qr_parser_parse(parser, page);
  ASSERT_EQ_INT("long tp part index", parsed, 0);
  ASSERT_EQ_INT("long tp format", qr_parser_get_format(parser), FORMAT_TP_MULTI);
  ASSERT_TRUE("long tp complete", qr_parser_is_complete(parser));

  size_t result_len = 0;
  char *result = qr_parser_result(parser, &result_len);
  ASSERT_TRUE("long tp result", result != NULL);
  ASSERT_EQ_INT("long tp result len", (int)result_len, (int)payload_len);
  ASSERT_STREQ("long tp payload", result, payload);

  free(result);
  qr_parser_destroy(parser);
  free(page);
  free(payload);
}

static void test_non_nul_pmofn_buffer(void) {
  const char payload[] = {'p', '1', 'o', 'f', '1', ' ', 'h', 'e', 'l',
                          'l', 'o', 'p', '2', 'o', 'f', '2', ' ', 'x'};
  const size_t payload_len = strlen("p1of1 hello");

  QRPartParser *parser = qr_parser_create();
  ASSERT_TRUE("create parser", parser != NULL);
  int parsed = qr_parser_parse_with_len(parser, payload, payload_len);
  ASSERT_EQ_INT("non nul pmofn index", parsed, 0);
  ASSERT_EQ_INT("non nul pmofn format", qr_parser_get_format(parser),
                FORMAT_PMOFN);
  ASSERT_EQ_INT("non nul pmofn count", qr_parser_parsed_count(parser), 1);
  ASSERT_EQ_INT("non nul pmofn total", qr_parser_total_count(parser), 1);
  ASSERT_TRUE("non nul pmofn complete", qr_parser_is_complete(parser));

  size_t result_len = 0;
  char *result = qr_parser_result(parser, &result_len);
  ASSERT_TRUE("non nul pmofn result", result != NULL);
  ASSERT_EQ_INT("non nul pmofn result len", (int)result_len, 5);
  ASSERT_STREQ("non nul pmofn payload", result, "hello");
  free(result);
  qr_parser_destroy(parser);
}

static void test_duplicate_pmofn_part_updates(void) {
  QRPartParser *parser = qr_parser_create();
  ASSERT_TRUE("create parser", parser != NULL);
  ASSERT_EQ_INT("duplicate first part", qr_parser_parse(parser, "p1of1 old"),
                0);
  ASSERT_EQ_INT("duplicate replacement",
                qr_parser_parse(parser, "p1of1 replacement"), 0);
  ASSERT_EQ_INT("duplicate count remains unique", qr_parser_parsed_count(parser),
                1);
  ASSERT_TRUE("duplicate complete", qr_parser_is_complete(parser));

  size_t result_len = 0;
  char *result = qr_parser_result(parser, &result_len);
  ASSERT_TRUE("duplicate result", result != NULL);
  ASSERT_EQ_INT("duplicate result len", (int)result_len,
                (int)strlen("replacement"));
  ASSERT_STREQ("duplicate payload", result, "replacement");
  free(result);
  qr_parser_destroy(parser);
}

static void test_uppercase_pmofn_header(void) {
  QRPartParser *parser = qr_parser_create();
  ASSERT_TRUE("create parser", parser != NULL);
  ASSERT_EQ_INT("uppercase pmofn part", qr_parser_parse(parser, "P1OF1 hello"),
                0);
  ASSERT_EQ_INT("uppercase pmofn format", qr_parser_get_format(parser),
                FORMAT_PMOFN);
  ASSERT_TRUE("uppercase pmofn complete", qr_parser_is_complete(parser));

  size_t result_len = 0;
  char *result = qr_parser_result(parser, &result_len);
  ASSERT_TRUE("uppercase pmofn result", result != NULL);
  ASSERT_EQ_INT("uppercase pmofn result len", (int)result_len, 5);
  ASSERT_STREQ("uppercase pmofn payload", result, "hello");
  free(result);
  qr_parser_destroy(parser);
}

static void test_invalid_ur_falls_back_to_raw(void) {
  const char *payload = "ur:eth-sign-request/not-standard";
  QRPartParser *parser = qr_parser_create();
  ASSERT_TRUE("create parser", parser != NULL);
  ASSERT_EQ_INT("invalid ur raw index", qr_parser_parse(parser, payload), 0);
  ASSERT_EQ_INT("invalid ur raw format", qr_parser_get_format(parser),
                FORMAT_NONE);
  ASSERT_TRUE("invalid ur raw complete", qr_parser_is_complete(parser));

  size_t result_len = 0;
  char *result = qr_parser_result(parser, &result_len);
  ASSERT_TRUE("invalid ur raw result", result != NULL);
  ASSERT_EQ_INT("invalid ur raw result len", (int)result_len,
                (int)strlen(payload));
  ASSERT_STREQ("invalid ur raw payload", result, payload);
  free(result);
  qr_parser_destroy(parser);
}

static void assert_invalid_pmofn(const char *name, const char *payload) {
  QRPartParser *parser = qr_parser_create();
  ASSERT_TRUE("create parser", parser != NULL);
  ASSERT_EQ_INT(name, qr_parser_parse(parser, payload), -1);
  ASSERT_EQ_INT("invalid pmofn count", qr_parser_parsed_count(parser), 0);
  ASSERT_TRUE("invalid pmofn incomplete", !qr_parser_is_complete(parser));
  qr_parser_destroy(parser);
}

static void test_invalid_pmofn_headers(void) {
  assert_invalid_pmofn("pmofn zero index", "p0of1 data");
  assert_invalid_pmofn("pmofn zero total", "p1of0 data");
  assert_invalid_pmofn("pmofn index exceeds total", "p2of1 data");
  assert_invalid_pmofn("pmofn total too large", "p1of257 data");
  assert_invalid_pmofn("pmofn trailing header junk", "p1of2x data");
  assert_invalid_pmofn("pmofn empty part", "p1of1 ");
}

int main(void) {
  test_tpr1_relay_pages();
  test_w3r1_relay_pages();
  test_tp_multifragment_pages();
  test_tp_multifragment_long_content();
  test_non_nul_pmofn_buffer();
  test_duplicate_pmofn_part_updates();
  test_uppercase_pmofn_header();
  test_invalid_ur_falls_back_to_raw();
  test_invalid_pmofn_headers();
  puts("PASS: qr relay parser");
  return 0;
}
