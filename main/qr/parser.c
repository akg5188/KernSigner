#include "parser.h"
#include "../../components/bbqr/src/bbqr.h"
#include "../../components/bbqr/src/miniz.h"
#include "../../components/cUR/src/crc32.h"
#include "../../components/cUR/src/ur_decoder.h"
#include <ctype.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// QR capacity arrays (limited to version 20)
static const int QR_CAPACITY_BYTE[] = {17,  32,  53,  78,  106, 134, 154,
                                       192, 230, 271, 321, 367, 425, 458,
                                       520, 586, 644, 718, 792, 858};

static const int QR_CAPACITY_ALPHANUMERIC[] = {
    25,  47,  77,  114, 154, 195, 224, 279,  335,  395,
    468, 535, 619, 667, 758, 854, 938, 1046, 1153, 1249};

#define QR_CAPACITY_SIZE 20
#define RELAY_MAX_PARTS 256
#define RELAY_MAX_ENCODED_LEN (256 * 1024)
#define QR_PARSER_MAX_INPUT_LEN (1024 * 1024)

// Helper function prototypes
static int detect_format(const char *data, size_t data_len, BBQrCode **bbqr);
static bool parse_pmofn_qr_part(const char *data, char **part, int *index,
                                int *total);
static bool parse_relay_qr_part(const char *data, size_t data_len, char **part,
                                int *index, int *total);
static bool parse_tp_multi_fragment_qr_part(const char *data, size_t data_len,
                                            char **part, int *index,
                                            int *total);
static bool starts_with_case_insensitive(const char *str, const char *prefix);
static bool parse_uint_range(const char **p, int min, int max, int *out);
static int max_qr_bytes(int max_width, const char *encoding);
static void find_min_num_parts(const char *data, size_t data_len, int max_width,
                               int qr_format, int *num_parts, int *part_size);
static bool add_part(QRPartParser *parser, int index, const char *data,
                     size_t data_len);
static bool fallback_to_single_raw(QRPartParser *parser, const char *data,
                                   size_t data_len);
static char *decode_relay_result(QRPartParser *parser, size_t *result_len);
static char *decode_tp_multi_result(QRPartParser *parser, size_t *result_len);
static int compare_parts(const void *a, const void *b);

QRPartParser *qr_parser_create(void) {
  QRPartParser *parser = (QRPartParser *)calloc(1, sizeof(QRPartParser));
  if (!parser)
    return NULL;

  parser->parts_capacity = 10;
  parser->parts = (QRPart **)calloc(parser->parts_capacity, sizeof(QRPart *));
  if (!parser->parts) {
    free(parser);
    return NULL;
  }

  parser->total = -1;
  parser->format = -1;
  return parser;
}

void qr_parser_destroy(QRPartParser *parser) {
  if (!parser)
    return;

  if (parser->parts) {
    for (int i = 0; i < parser->parts_count; i++) {
      if (parser->parts[i]) {
        free(parser->parts[i]->data);
        free(parser->parts[i]);
      }
    }
    free(parser->parts);
  }

  if (parser->bbqr) {
    free(parser->bbqr->payload);
    free(parser->bbqr);
  }

  if (parser->ur_decoder) {
    ur_decoder_free((ur_decoder_t *)parser->ur_decoder);
    parser->ur_decoder = NULL;
  }

  free(parser);
}

int qr_parser_parsed_count(QRPartParser *parser) {
  if (parser->format == FORMAT_UR && parser->ur_decoder) {
    ur_decoder_t *decoder = (ur_decoder_t *)parser->ur_decoder;
    return (int)ur_decoder_processed_parts_count(decoder);
  }
  return parser->parts_count;
}

int qr_parser_processed_parts_count(QRPartParser *parser) {
  if (parser->format == FORMAT_UR && parser->ur_decoder) {
    ur_decoder_t *decoder = (ur_decoder_t *)parser->ur_decoder;
    return (int)ur_decoder_processed_parts_count(decoder);
  }
  return parser->parts_count;
}

int qr_parser_total_count(QRPartParser *parser) {
  if (parser->format == FORMAT_UR && parser->ur_decoder) {
    ur_decoder_t *decoder = (ur_decoder_t *)parser->ur_decoder;
    size_t expected = ur_decoder_expected_part_count(decoder);
    return expected > 0 ? (int)expected : 1;
  }
  return parser->total;
}

static bool add_part(QRPartParser *parser, int index, const char *data,
                     size_t data_len) {
  if (!parser || !data || data_len > QR_PARSER_MAX_INPUT_LEN)
    return false;

  // Resize if needed
  if (parser->parts_count >= parser->parts_capacity) {
    int new_capacity = parser->parts_capacity * 2;
    QRPart **new_parts =
        (QRPart **)realloc(parser->parts, new_capacity * sizeof(QRPart *));
    if (!new_parts)
      return false;
    parser->parts = new_parts;
    parser->parts_capacity = new_capacity;
  }

  // Check if part already exists
  for (int i = 0; i < parser->parts_count; i++) {
    if (parser->parts[i]->index == index) {
      // Update existing part
      char *new_data = (char *)malloc(data_len + 1);
      if (!new_data)
        return false;
      memcpy(new_data, data, data_len);
      new_data[data_len] = '\0';
      free(parser->parts[i]->data);
      parser->parts[i]->data = new_data;
      parser->parts[i]->data_len = data_len;
      return true;
    }
  }

  // Add new part
  QRPart *part = (QRPart *)calloc(1, sizeof(QRPart));
  if (!part)
    return false;

  part->index = index;
  part->data = (char *)malloc(data_len + 1);
  if (!part->data) {
    free(part);
    return false;
  }
  memcpy(part->data, data, data_len);
  part->data[data_len] = '\0';
  part->data_len = data_len;

  parser->parts[parser->parts_count++] = part;
  return true;
}

static bool fallback_to_single_raw(QRPartParser *parser, const char *data,
                                   size_t data_len) {
  if (!parser || !data || data_len == 0)
    return false;

  if (parser->ur_decoder) {
    ur_decoder_free((ur_decoder_t *)parser->ur_decoder);
    parser->ur_decoder = NULL;
  }
  if (parser->bbqr) {
    free(parser->bbqr->payload);
    free(parser->bbqr);
    parser->bbqr = NULL;
  }

  parser->format = FORMAT_NONE;
  parser->total = 1;
  return add_part(parser, 1, data, data_len);
}

int qr_parser_parse(QRPartParser *parser, const char *data) {
  if (!parser || !data)
    return -1;
  return qr_parser_parse_with_len(parser, data, strlen(data));
}

int qr_parser_parse_with_len(QRPartParser *parser, const char *data,
                             size_t data_len) {
  if (!parser || !data || data_len == 0 || data_len > QR_PARSER_MAX_INPUT_LEN)
    return -1;

  char *safe_data = (char *)malloc(data_len + 1);
  if (!safe_data)
    return -1;
  memcpy(safe_data, data, data_len);
  safe_data[data_len] = '\0';

  int parsed_index = -1;

  if (parser->format == -1) {
    parser->format = detect_format(safe_data, data_len, &parser->bbqr);
  }

  if (parser->format == FORMAT_NONE) {
    if (!add_part(parser, 1, data, data_len))
      goto cleanup;
    parser->total = 1;
    parsed_index = 0;
  } else if (parser->format == FORMAT_PMOFN) {
    char *part = NULL;
    int index, total;
    if (parse_pmofn_qr_part(safe_data, &part, &index, &total)) {
      if (!add_part(parser, index, part, strlen(part))) {
        free(part);
        goto cleanup;
      }
      parser->total = total;
      free(part);
      parsed_index = index - 1;
    }
  } else if (parser->format == FORMAT_UR) {
    // Create UR decoder if not exists
    if (!parser->ur_decoder) {
      parser->ur_decoder = ur_decoder_new();
      if (!parser->ur_decoder) {
        goto cleanup;
      }
    }

    ur_decoder_t *decoder = (ur_decoder_t *)parser->ur_decoder;
    if (ur_decoder_receive_part(decoder, safe_data)) {
      if (ur_decoder_is_complete(decoder)) {
        parsed_index = 0; // Single-part UR, complete immediately
        goto cleanup;
      }
      size_t processed = ur_decoder_processed_parts_count(decoder);
      parsed_index = (int)processed - 1;
    } else if (parser->parts_count == 0 &&
               ur_decoder_processed_parts_count(decoder) == 0 &&
               fallback_to_single_raw(parser, data, data_len)) {
      // Some wallet QR payloads are URI-like but not strict BC-UR. Return the
      // raw payload to the higher-level scanner instead of silently waiting
      // forever on a decoder that will never complete.
      parsed_index = 0;
    }
  } else if (parser->format == FORMAT_BBQR) {
    BBQrPart part;
    if (bbqr_parse_part(safe_data, data_len, &part)) {
      // Store payload (payload_len may differ from strlen if binary)
      if (!add_part(parser, part.index, part.payload, part.payload_len))
        goto cleanup;
      parser->total = part.total;
      parsed_index = part.index;
    }
  } else if (parser->format == FORMAT_RELAY) {
    char *part = NULL;
    int index, total;
    if (parse_relay_qr_part(safe_data, data_len, &part, &index, &total)) {
      if (!add_part(parser, index, part, strlen(part))) {
        free(part);
        goto cleanup;
      }
      parser->total = total;
      free(part);
      parsed_index = index - 1;
    }
  } else if (parser->format == FORMAT_TP_MULTI) {
    char *part = NULL;
    int index, total;
    if (parse_tp_multi_fragment_qr_part(safe_data, data_len, &part, &index,
                                        &total)) {
      if (!add_part(parser, index, part, strlen(part))) {
        free(part);
        goto cleanup;
      }
      parser->total = total;
      int zero_based = (index >= 1 && index <= total) ? index - 1 : index;
      free(part);
      parsed_index = zero_based;
    }
  }

cleanup:
  free(safe_data);
  return parsed_index;
}

bool qr_parser_is_complete(QRPartParser *parser) {
  if (parser->format == FORMAT_UR && parser->ur_decoder) {
    ur_decoder_t *decoder = (ur_decoder_t *)parser->ur_decoder;
    return ur_decoder_is_complete(decoder) && ur_decoder_is_success(decoder);
  }

  if (parser->total == -1)
    return false;
  if (parser->parts_count != parser->total)
    return false;

  if (parser->format == FORMAT_TP_MULTI) {
    bool has_one_based = true;
    bool has_zero_based = true;
    for (int expected = 1; expected <= parser->total; expected++) {
      bool found = false;
      for (int i = 0; i < parser->parts_count; i++) {
        if (parser->parts[i]->index == expected) {
          found = true;
          break;
        }
      }
      if (!found) {
        has_one_based = false;
        break;
      }
    }
    for (int expected = 0; expected < parser->total; expected++) {
      bool found = false;
      for (int i = 0; i < parser->parts_count; i++) {
        if (parser->parts[i]->index == expected) {
          found = true;
          break;
        }
      }
      if (!found) {
        has_zero_based = false;
        break;
      }
    }
    return has_one_based || has_zero_based;
  }

  int start_index = (parser->format == FORMAT_PMOFN ||
                     parser->format == FORMAT_NONE ||
                     parser->format == FORMAT_RELAY)
                        ? 1
                        : 0;

  for (int expected = start_index; expected < start_index + parser->total;
       expected++) {
    bool found = false;
    for (int i = 0; i < parser->parts_count; i++) {
      if (parser->parts[i]->index == expected) {
        found = true;
        break;
      }
    }
    if (!found)
      return false;
  }

  return true;
}

static int compare_parts(const void *a, const void *b) {
  QRPart *part_a = *(QRPart **)a;
  QRPart *part_b = *(QRPart **)b;
  return part_a->index - part_b->index;
}

char *qr_parser_result(QRPartParser *parser, size_t *result_len) {
  if (parser->format == FORMAT_UR && parser->ur_decoder) {
    // For UR format, return a special marker string that indicates
    // the result needs to be extracted using qr_parser_get_ur_result()
    // This is because UR results are binary CBOR data, not text strings
    const char *marker = "UR_RESULT";
    char *result = strdup(marker);
    if (result_len) {
      *result_len = strlen(marker);
    }
    return result;
  }

  if (parser->format == FORMAT_BBQR) {
    // Sort parts by index
    qsort(parser->parts, parser->parts_count, sizeof(QRPart *), compare_parts);

    // Calculate total payload length
    size_t total_payload_len = 0;
    for (int i = 0; i < parser->parts_count; i++) {
      if (total_payload_len + parser->parts[i]->data_len < total_payload_len ||
          total_payload_len + parser->parts[i]->data_len > 1024 * 1024) {
        return NULL;
      }
      total_payload_len += parser->parts[i]->data_len;
    }

    // Combine payloads
    char *combined = (char *)malloc(total_payload_len + 1);
    if (!combined) {
      return NULL;
    }

    size_t offset = 0;
    for (int i = 0; i < parser->parts_count; i++) {
      memcpy(combined + offset, parser->parts[i]->data,
             parser->parts[i]->data_len);
      offset += parser->parts[i]->data_len;
    }
    combined[total_payload_len] = '\0';

    // Decode payload (base32/hex decode + optional decompression)
    size_t decoded_len = 0;
    uint8_t *decoded = bbqr_decode_payload(parser->bbqr->encoding, combined,
                                           total_payload_len, &decoded_len);
    free(combined);

    if (!decoded) {
      return NULL;
    }

    // Store decoded payload in bbqr structure
    if (parser->bbqr->payload) {
      free(parser->bbqr->payload);
    }
    parser->bbqr->payload = (char *)decoded;

    if (result_len) {
      *result_len = decoded_len;
    }

    // Return a copy of the decoded data
    char *result = (char *)malloc(decoded_len + 1);
    if (!result) {
      return NULL;
    }
    memcpy(result, decoded, decoded_len);
    result[decoded_len] = '\0';
    return result;
  }

  if (parser->format == FORMAT_RELAY) {
    return decode_relay_result(parser, result_len);
  }

  if (parser->format == FORMAT_TP_MULTI) {
    return decode_tp_multi_result(parser, result_len);
  }

  // Sort parts by index
  qsort(parser->parts, parser->parts_count, sizeof(QRPart *), compare_parts);

  // Calculate total length
  size_t total_len = 0;
  for (int i = 0; i < parser->parts_count; i++) {
    if (total_len + parser->parts[i]->data_len < total_len ||
        total_len + parser->parts[i]->data_len > 1024 * 1024) {
      return NULL;
    }
    total_len += parser->parts[i]->data_len;
  }

  // Combine parts
  char *result = (char *)malloc(total_len + 1);
  if (!result)
    return NULL;

  size_t offset = 0;
  for (int i = 0; i < parser->parts_count; i++) {
    memcpy(result + offset, parser->parts[i]->data, parser->parts[i]->data_len);
    offset += parser->parts[i]->data_len;
  }
  result[total_len] = '\0';

  if (result_len)
    *result_len = total_len;
  return result;
}

static bool starts_with_case_insensitive(const char *str, const char *prefix) {
  while (*prefix) {
    if (tolower((unsigned char)*str) != tolower((unsigned char)*prefix))
      return false;
    str++;
    prefix++;
  }
  return true;
}

static int detect_format(const char *data, size_t data_len, BBQrCode **bbqr) {
  if (data[0] == 'p' || data[0] == 'P') {
    // Check for "pXofY " format
    const char *space = strchr(data, ' ');
    if (space) {
      char header[32];
      size_t header_len = space - data;
      if (header_len < sizeof(header)) {
        strncpy(header, data, header_len);
        header[header_len] = '\0';

        const char *of_pos = strstr(header, "of");
        if (!of_pos)
          of_pos = strstr(header, "OF");
        if (of_pos && of_pos > header + 1) {
          // Check if number before "of"
          bool is_digit = true;
          for (const char *p = header + 1; p < of_pos; p++) {
            if (!isdigit((unsigned char)*p)) {
              is_digit = false;
              break;
            }
          }
          if (is_digit)
            return FORMAT_PMOFN;
        }
      }
    }
  } else if (starts_with_case_insensitive(data, "ur:")) {
    return FORMAT_UR;
  } else if (starts_with_case_insensitive(data, "tpr1:") ||
             starts_with_case_insensitive(data, "w3r1:")) {
    return FORMAT_RELAY;
  } else if (starts_with_case_insensitive(data, "tp:multiFragment-")) {
    return FORMAT_TP_MULTI;
  } else if (data_len >= BBQR_HEADER_LEN && strncmp(data, "B$", 2) == 0) {
    // Validate BBQr header (convert to uppercase for validation)
    char encoding = toupper((unsigned char)data[2]);
    char file_type = toupper((unsigned char)data[3]);
    if (bbqr_is_valid_encoding(encoding) &&
        bbqr_is_valid_file_type(file_type)) {
      // Create BBQrCode structure
      *bbqr = (BBQrCode *)calloc(1, sizeof(BBQrCode));
      if (*bbqr) {
        (*bbqr)->encoding = encoding;
        (*bbqr)->file_type = file_type;
        (*bbqr)->payload = NULL;
      }
      return FORMAT_BBQR;
    }
  }

  return FORMAT_NONE;
}

static bool parse_pmofn_qr_part(const char *data, char **part, int *index,
                                int *total) {
  if (!data || !part || !index || !total ||
      (data[0] != 'p' && data[0] != 'P'))
    return false;

  const char *p = data + 1;
  int parsed_index = 0;
  if (!parse_uint_range(&p, 1, RELAY_MAX_PARTS, &parsed_index))
    return false;

  const char *of_pos = p;
  if (!((of_pos[0] == 'o' || of_pos[0] == 'O') &&
        (of_pos[1] == 'f' || of_pos[1] == 'F')))
    return false;
  p = of_pos + 2;

  int parsed_total = 0;
  if (!parse_uint_range(&p, 1, RELAY_MAX_PARTS, &parsed_total))
    return false;

  if (parsed_index > parsed_total)
    return false;

  const char *space_pos = strchr(data, ' ');
  if (!space_pos || p != space_pos)
    return false;

  // Extract part data (after space)
  size_t part_len = strlen(space_pos + 1);
  if (part_len == 0)
    return false;
  *part = (char *)malloc(part_len + 1);
  if (!*part)
    return false;
  memcpy(*part, space_pos + 1, part_len);
  (*part)[part_len] = '\0';

  *index = parsed_index;
  *total = parsed_total;
  return true;
}

static bool parse_uint_range(const char **p, int min, int max, int *out) {
  if (!p || !*p || !out || !isdigit((unsigned char)**p))
    return false;

  int value = 0;
  while (isdigit((unsigned char)**p)) {
    int digit = **p - '0';
    if (value > (max - digit) / 10)
      return false;
    value = value * 10 + digit;
    (*p)++;
  }

  if (value < min || value > max)
    return false;

  *out = value;
  return true;
}

static bool parse_relay_qr_part(const char *data, size_t data_len, char **part,
                                int *index, int *total) {
  if (!data || !part || !index || !total || data_len < 10)
    return false;

  if (!starts_with_case_insensitive(data, "tpr1:") &&
      !starts_with_case_insensitive(data, "w3r1:"))
    return false;

  const char *p = data + 5;
  int parsed_index = 0;
  int parsed_total = 0;
  if (!parse_uint_range(&p, 1, RELAY_MAX_PARTS, &parsed_index) || *p != '/')
    return false;
  p++;
  if (!parse_uint_range(&p, 1, RELAY_MAX_PARTS, &parsed_total) || *p != '.')
    return false;
  if (parsed_index > parsed_total)
    return false;
  p++;

  const char *crc_start = p;
  int crc_digits = 0;
  while (isdigit((unsigned char)*p)) {
    crc_digits++;
    p++;
  }
  if (crc_digits == 0 || *p != '.')
    return false;
  p++;

  const char *chunk_start = p;
  size_t chunk_len = data_len - (size_t)(chunk_start - data);
  if (chunk_len == 0)
    return false;

  size_t crc_len = (size_t)crc_digits;
  size_t out_len = crc_len + 1 + chunk_len;
  char *out = (char *)malloc(out_len + 1);
  if (!out)
    return false;

  memcpy(out, crc_start, crc_len);
  out[crc_len] = '.';
  memcpy(out + crc_len + 1, chunk_start, chunk_len);
  out[out_len] = '\0';

  *part = out;
  *index = parsed_index;
  *total = parsed_total;
  return true;
}

static int base64url_value(char c) {
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '-' || c == '+')
    return 62;
  if (c == '_' || c == '/')
    return 63;
  return -1;
}

static uint8_t *base64url_decode_alloc(const char *input, size_t input_len,
                                       size_t *out_len) {
  if (!input || !out_len)
    return NULL;

  size_t max_len = (input_len * 3) / 4 + 3;
  uint8_t *out = (uint8_t *)malloc(max_len);
  if (!out)
    return NULL;

  uint32_t acc = 0;
  int bits = 0;
  size_t pos = 0;
  for (size_t i = 0; i < input_len; i++) {
    char c = input[i];
    if (c == '=')
      break;
    if (isspace((unsigned char)c))
      continue;
    int value = base64url_value(c);
    if (value < 0) {
      free(out);
      return NULL;
    }
    acc = (acc << 6) | (uint32_t)value;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (pos >= max_len) {
        free(out);
        return NULL;
      }
      out[pos++] = (uint8_t)((acc >> bits) & 0xff);
    }
  }

  *out_len = pos;
  return out;
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static char *url_decode_alloc(const char *input, size_t input_len,
                              size_t *out_len) {
  char *out = (char *)malloc(input_len + 1);
  if (!out)
    return NULL;

  size_t pos = 0;
  for (size_t i = 0; i < input_len; i++) {
    char c = input[i];
    if (c == '%' && i + 2 < input_len) {
      int hi = hex_value(input[i + 1]);
      int lo = hex_value(input[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out[pos++] = (char)((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    out[pos++] = (c == '+') ? ' ' : c;
  }
  out[pos] = '\0';
  if (out_len)
    *out_len = pos;
  return out;
}

static bool query_param_decode(const char *query, size_t query_len,
                               const char *key, char **out) {
  if (!query || !key || !out)
    return false;
  *out = NULL;

  size_t key_len = strlen(key);
  const char *p = query;
  const char *end = query + query_len;
  while (p < end) {
    const char *part_end = memchr(p, '&', (size_t)(end - p));
    if (!part_end)
      part_end = end;

    const char *eq = memchr(p, '=', (size_t)(part_end - p));
    if (eq && (size_t)(eq - p) == key_len && strncmp(p, key, key_len) == 0) {
      size_t raw_len = (size_t)(part_end - eq - 1);
      *out = url_decode_alloc(eq + 1, raw_len, NULL);
      return *out != NULL;
    }

    p = part_end + (part_end < end ? 1 : 0);
  }

  return false;
}

static bool json_simple_string_value(const char *json, const char *key,
                                     char *out, size_t out_len) {
  if (!json || !key || !out || out_len == 0)
    return false;
  out[0] = '\0';

  char pattern[48];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *p = strstr(json, pattern);
  if (!p)
    return false;
  p += strlen(pattern);
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != ':')
    return false;
  p++;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != '"')
    return false;
  p++;

  size_t pos = 0;
  while (*p && *p != '"' && pos + 1 < out_len) {
    if (*p == '\\' && p[1]) {
      p++;
      switch (*p) {
      case 'n':
        out[pos++] = '\n';
        break;
      case 'r':
        out[pos++] = '\r';
        break;
      case 't':
        out[pos++] = '\t';
        break;
      default:
        out[pos++] = *p;
        break;
      }
    } else {
      out[pos++] = *p;
    }
    p++;
  }
  out[pos] = '\0';
  return *p == '"';
}

static char *json_simple_string_value_alloc(const char *json, const char *key) {
  if (!json || !key)
    return NULL;

  char pattern[48];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *p = strstr(json, pattern);
  if (!p)
    return NULL;
  p += strlen(pattern);
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != ':')
    return NULL;
  p++;
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != '"')
    return NULL;
  p++;

  char *out = (char *)malloc(strlen(p) + 1);
  if (!out)
    return NULL;

  size_t pos = 0;
  while (*p && *p != '"') {
    if (*p == '\\' && p[1]) {
      p++;
      switch (*p) {
      case 'n':
        out[pos++] = '\n';
        break;
      case 'r':
        out[pos++] = '\r';
        break;
      case 't':
        out[pos++] = '\t';
        break;
      default:
        out[pos++] = *p;
        break;
      }
    } else {
      out[pos++] = *p;
    }
    p++;
  }

  if (*p != '"') {
    free(out);
    return NULL;
  }

  out[pos] = '\0';
  return out;
}

static bool json_simple_int_value(const char *json, const char *key,
                                  int *out) {
  if (!json || !key || !out)
    return false;

  char text[32];
  if (json_simple_string_value(json, key, text, sizeof(text))) {
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (end != text) {
      *out = (int)value;
      return true;
    }
  }

  char pattern[48];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *p = strstr(json, pattern);
  if (!p)
    return false;
  p += strlen(pattern);
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != ':')
    return false;
  p++;
  while (*p && isspace((unsigned char)*p))
    p++;
  char *end = NULL;
  long value = strtol(p, &end, 10);
  if (end == p)
    return false;
  *out = (int)value;
  return true;
}

static bool parse_fragment_index_total(const char *text, int *index,
                                       int *total) {
  if (!text || !index || !total)
    return false;
  const char *slash = strchr(text, '/');
  if (!slash)
    slash = strchr(text, '-');
  if (!slash || slash == text || slash[1] == '\0')
    return false;

  char *end = NULL;
  long idx = strtol(text, &end, 10);
  if (end != slash)
    return false;
  long tot = strtol(slash + 1, &end, 10);
  if (end == slash + 1)
    return false;
  *index = (int)idx;
  *total = (int)tot;
  return true;
}

static bool parse_tp_multi_fragment_qr_part(const char *data, size_t data_len,
                                            char **part, int *index,
                                            int *total) {
  if (!data || !part || !index || !total ||
      !starts_with_case_insensitive(data, "tp:multiFragment-"))
    return false;

  const char *dash = memchr(data, '-', data_len);
  if (!dash)
    return false;
  const char *query = dash + 1;
  if (*query == '?')
    query++;
  size_t query_len = data_len - (size_t)(query - data);

  char *data_json = NULL;
  if (!query_param_decode(query, query_len, "data", &data_json))
    return false;

  char index_text[32];
  char total_text[32];
  char *content = json_simple_string_value_alloc(data_json, "content");
  if (!content) {
    free(data_json);
    return false;
  }

  int parsed_index = -1;
  int parsed_total = -1;
  if (json_simple_string_value(data_json, "index", index_text,
                               sizeof(index_text))) {
    if (!parse_fragment_index_total(index_text, &parsed_index,
                                    &parsed_total)) {
      char *end = NULL;
      long idx = strtol(index_text, &end, 10);
      if (end != index_text)
        parsed_index = (int)idx;
    }
  } else {
    (void)json_simple_int_value(data_json, "index", &parsed_index);
  }

  if (parsed_total < 0) {
    if (json_simple_string_value(data_json, "total", total_text,
                                 sizeof(total_text))) {
      char *end = NULL;
      long tot = strtol(total_text, &end, 10);
      if (end != total_text)
        parsed_total = (int)tot;
    } else if (!json_simple_int_value(data_json, "total", &parsed_total)) {
      if (json_simple_string_value(data_json, "count", total_text,
                                   sizeof(total_text)) ||
          json_simple_string_value(data_json, "size", total_text,
                                   sizeof(total_text))) {
        char *end = NULL;
        long tot = strtol(total_text, &end, 10);
        if (end != total_text)
          parsed_total = (int)tot;
      } else if (!json_simple_int_value(data_json, "count", &parsed_total)) {
        (void)json_simple_int_value(data_json, "size", &parsed_total);
      }
    }
  }

  free(data_json);

  if (parsed_total <= 0 || parsed_total > RELAY_MAX_PARTS ||
      parsed_index < 0 || parsed_index > parsed_total) {
    free(content);
    return false;
  }

  char *split = strrchr(content, '_');
  if (!split || split == content || split[1] == '\0') {
    free(content);
    return false;
  }

  size_t chunk_len = (size_t)(split - content);
  const char *crc = split + 1;
  size_t crc_len = strlen(crc);
  size_t out_len = crc_len + 1 + chunk_len;
  char *out = (char *)malloc(out_len + 1);
  if (!out) {
    free(content);
    return false;
  }
  memcpy(out, crc, crc_len);
  out[crc_len] = '.';
  memcpy(out + crc_len + 1, content, chunk_len);
  out[out_len] = '\0';
  free(content);

  *part = out;
  *index = parsed_index;
  *total = parsed_total;
  return true;
}

static char *decode_relay_result(QRPartParser *parser, size_t *result_len) {
  qsort(parser->parts, parser->parts_count, sizeof(QRPart *), compare_parts);

  const char *expected_crc = NULL;
  size_t expected_crc_len = 0;
  size_t encoded_len = 0;
  for (int i = 0; i < parser->parts_count; i++) {
    const char *dot = memchr(parser->parts[i]->data, '.',
                             parser->parts[i]->data_len);
    if (!dot || dot == parser->parts[i]->data)
      return NULL;

    size_t crc_len = (size_t)(dot - parser->parts[i]->data);
    if (!expected_crc) {
      expected_crc = parser->parts[i]->data;
      expected_crc_len = crc_len;
    } else if (crc_len != expected_crc_len ||
               memcmp(expected_crc, parser->parts[i]->data, crc_len) != 0) {
      return NULL;
    }

    size_t chunk_len = parser->parts[i]->data_len - crc_len - 1;
    if (encoded_len + chunk_len < encoded_len ||
        encoded_len + chunk_len > RELAY_MAX_ENCODED_LEN)
      return NULL;
    encoded_len += chunk_len;
  }

  char *encoded = (char *)malloc(encoded_len + 1);
  if (!encoded)
    return NULL;

  size_t offset = 0;
  for (int i = 0; i < parser->parts_count; i++) {
    const char *dot = strchr(parser->parts[i]->data, '.');
    if (!dot) {
      free(encoded);
      return NULL;
    }
    size_t chunk_len = parser->parts[i]->data_len -
                       (size_t)(dot + 1 - parser->parts[i]->data);
    memcpy(encoded + offset, dot + 1, chunk_len);
    offset += chunk_len;
  }
  encoded[encoded_len] = '\0';

  uint32_t actual_crc =
      crc32_calculate((const uint8_t *)encoded, encoded_len);
  char actual_crc_text[16];
  snprintf(actual_crc_text, sizeof(actual_crc_text), "%lu",
           (unsigned long)actual_crc);
  if (strlen(actual_crc_text) != expected_crc_len ||
      memcmp(actual_crc_text, expected_crc, expected_crc_len) != 0) {
    free(encoded);
    return NULL;
  }

  size_t compressed_len = 0;
  uint8_t *compressed =
      base64url_decode_alloc(encoded, encoded_len, &compressed_len);
  free(encoded);
  if (!compressed)
    return NULL;

  size_t decoded_len = 0;
  uint8_t *decoded =
      mz_uncompress_alloc(compressed, compressed_len, &decoded_len);
  if (!decoded)
    decoded = mz_inflate_raw_alloc(compressed, compressed_len, &decoded_len);
  free(compressed);
  if (!decoded)
    return NULL;

  char *result = (char *)malloc(decoded_len + 1);
  if (!result) {
    free(decoded);
    return NULL;
  }
  memcpy(result, decoded, decoded_len);
  result[decoded_len] = '\0';
  free(decoded);

  if (result_len)
    *result_len = decoded_len;
  return result;
}

static char *decode_tp_multi_result(QRPartParser *parser, size_t *result_len) {
  qsort(parser->parts, parser->parts_count, sizeof(QRPart *), compare_parts);

  bool one_based = parser->parts_count > 0 && parser->parts[0]->index == 1;
  const char *expected_crc = NULL;
  size_t expected_crc_len = 0;
  size_t payload_len = 0;

  for (int i = 0; i < parser->parts_count; i++) {
    int expected_index = one_based ? i + 1 : i;
    if (parser->parts[i]->index != expected_index)
      return NULL;

    const char *dot = memchr(parser->parts[i]->data, '.',
                             parser->parts[i]->data_len);
    if (!dot || dot == parser->parts[i]->data)
      return NULL;

    size_t crc_len = (size_t)(dot - parser->parts[i]->data);
    if (!expected_crc) {
      expected_crc = parser->parts[i]->data;
      expected_crc_len = crc_len;
    } else if (crc_len != expected_crc_len ||
               memcmp(expected_crc, parser->parts[i]->data, crc_len) != 0) {
      return NULL;
    }

    size_t chunk_len = parser->parts[i]->data_len - crc_len - 1;
    if (payload_len + chunk_len < payload_len ||
        payload_len + chunk_len > RELAY_MAX_ENCODED_LEN)
      return NULL;
    payload_len += chunk_len;
  }

  char *payload = (char *)malloc(payload_len + 1);
  if (!payload)
    return NULL;

  size_t offset = 0;
  for (int i = 0; i < parser->parts_count; i++) {
    const char *dot = strchr(parser->parts[i]->data, '.');
    if (!dot) {
      free(payload);
      return NULL;
    }
    size_t chunk_len = parser->parts[i]->data_len -
                       (size_t)(dot + 1 - parser->parts[i]->data);
    memcpy(payload + offset, dot + 1, chunk_len);
    offset += chunk_len;
  }
  payload[payload_len] = '\0';

  uint32_t actual_crc =
      crc32_calculate((const uint8_t *)payload, payload_len);
  char actual_crc_text[16];
  snprintf(actual_crc_text, sizeof(actual_crc_text), "%lu",
           (unsigned long)actual_crc);
  if (strlen(actual_crc_text) != expected_crc_len ||
      memcmp(actual_crc_text, expected_crc, expected_crc_len) != 0) {
    free(payload);
    return NULL;
  }

  if (result_len)
    *result_len = payload_len;
  return payload;
}

static int max_qr_bytes(int max_width, const char *encoding) {
  max_width -= 2; // Subtract frame width
  int qr_version = (max_width - 17) / 4;

  if (qr_version < 1)
    qr_version = 1;
  if (qr_version > QR_CAPACITY_SIZE)
    qr_version = QR_CAPACITY_SIZE;

  const int *capacity_list = (strcmp(encoding, "alphanumeric") == 0)
                                 ? QR_CAPACITY_ALPHANUMERIC
                                 : QR_CAPACITY_BYTE;

  return capacity_list[qr_version - 1];
}

static void find_min_num_parts(const char *data, size_t data_len, int max_width,
                               int qr_format, int *num_parts, int *part_size) {
  const char *encoding = (qr_format == FORMAT_BBQR) ? "alphanumeric" : "byte";
  int qr_capacity = max_qr_bytes(max_width, encoding);

  if (qr_format == FORMAT_PMOFN) {
    int ps = qr_capacity - PMOFN_PREFIX_LENGTH_1D;
    *num_parts = (data_len + ps - 1) / ps;

    if (*num_parts > 9) {
      ps = qr_capacity - PMOFN_PREFIX_LENGTH_2D;
      *num_parts = (data_len + ps - 1) / ps;
    }

    *part_size = (data_len + *num_parts - 1) / *num_parts;
  } else if (qr_format == FORMAT_UR) {
    qr_capacity -= UR_GENERIC_PREFIX_LENGTH;
    qr_capacity -= (UR_CBOR_PREFIX_LEN + UR_BYTEWORDS_CRC_LEN) * 2;
    qr_capacity = (qr_capacity > UR_MIN_FRAGMENT_LENGTH)
                      ? qr_capacity
                      : UR_MIN_FRAGMENT_LENGTH;

    size_t adjusted_len = data_len * 2; // Bytewords encoding doubles length
    *num_parts = (adjusted_len + qr_capacity - 1) / qr_capacity;
    *part_size = data_len / *num_parts;
    *part_size = (*part_size > UR_MIN_FRAGMENT_LENGTH) ? *part_size
                                                       : UR_MIN_FRAGMENT_LENGTH;
  } else if (qr_format == FORMAT_BBQR) {
    int max_part_size = qr_capacity - BBQR_PREFIX_LENGTH;
    if ((int)data_len < max_part_size) {
      *num_parts = 1;
      *part_size = data_len;
      return;
    }

    max_part_size = (max_part_size / 8) * 8;
    *num_parts = (data_len + max_part_size - 1) / max_part_size;
    *part_size = data_len / *num_parts;
    *part_size = ((*part_size + 7) / 8) * 8;

    if (*part_size > max_part_size) {
      (*num_parts)++;
      *part_size = data_len / *num_parts;
      *part_size = ((*part_size + 7) / 8) * 8;
    }
  }
}

bool qr_parser_get_ur_result(QRPartParser *parser, const char **ur_type_out,
                             const uint8_t **cbor_data_out,
                             size_t *cbor_len_out) {
  if (!parser || parser->format != FORMAT_UR || !parser->ur_decoder) {
    return false;
  }

  ur_decoder_t *decoder = (ur_decoder_t *)parser->ur_decoder;
  if (!ur_decoder_is_complete(decoder) || !ur_decoder_is_success(decoder)) {
    return false;
  }

  ur_result_t *result = ur_decoder_get_result(decoder);
  if (!result) {
    return false;
  }

  if (ur_type_out) {
    *ur_type_out = result->type;
  }
  if (cbor_data_out) {
    *cbor_data_out = result->cbor_data;
  }
  if (cbor_len_out) {
    *cbor_len_out = result->cbor_len;
  }

  return true;
}

int qr_parser_get_format(QRPartParser *parser) {
  if (!parser) {
    return FORMAT_NONE;
  }
  return parser->format;
}

int get_qr_size(const char *qr_code) {
  int len = strlen(qr_code);
  int size = (int)sqrt(len * 8);
  return size;
}
