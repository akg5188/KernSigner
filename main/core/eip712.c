#include "eip712.h"
#include "evm.h"
#include "i18n/i18n.h"

#include <cJSON.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EIP712_MAX_TYPES 40
#define EIP712_MAX_TYPE_NAME 72

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} eip712_strbuf_t;

typedef struct {
  char names[EIP712_MAX_TYPES][EIP712_MAX_TYPE_NAME];
  size_t count;
} eip712_type_list_t;

typedef struct {
  char *err;
  size_t err_len;
} eip712_ctx_t;

static void eip712_set_error(eip712_ctx_t *ctx, const char *fmt, ...) {
  if (!ctx || !ctx->err || ctx->err_len == 0 || ctx->err[0])
    return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(ctx->err, ctx->err_len, fmt, ap);
  va_end(ap);
}

static bool eip712_sb_reserve(eip712_strbuf_t *sb, size_t extra) {
  if (!sb)
    return false;
  size_t need = sb->len + extra + 1;
  if (need <= sb->cap)
    return true;
  size_t cap = sb->cap ? sb->cap : 128;
  while (cap < need)
    cap *= 2;
  char *next = realloc(sb->buf, cap);
  if (!next)
    return false;
  sb->buf = next;
  sb->cap = cap;
  return true;
}

static bool eip712_sb_append_len(eip712_strbuf_t *sb, const char *text,
                                 size_t len) {
  if (!sb || (!text && len > 0))
    return false;
  if (!eip712_sb_reserve(sb, len))
    return false;
  if (len > 0)
    memcpy(sb->buf + sb->len, text, len);
  sb->len += len;
  sb->buf[sb->len] = '\0';
  return true;
}

static bool eip712_sb_append(eip712_strbuf_t *sb, const char *text) {
  return eip712_sb_append_len(sb, text, text ? strlen(text) : 0);
}

static char *eip712_sb_take(eip712_strbuf_t *sb) {
  if (!sb)
    return NULL;
  if (!sb->buf && !eip712_sb_append(sb, ""))
    return NULL;
  char *out = sb->buf;
  sb->buf = NULL;
  sb->len = 0;
  sb->cap = 0;
  return out;
}

static const char *eip712_json_string(const cJSON *item) {
  return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool eip712_is_array_type(const char *type) {
  size_t len = type ? strlen(type) : 0;
  return len > 0 && type[len - 1] == ']';
}

static bool eip712_core_type(const char *type, char *out, size_t out_len) {
  if (!type || !out || out_len == 0)
    return false;
  const char *bracket = strchr(type, '[');
  size_t len = bracket ? (size_t)(bracket - type) : strlen(type);
  if (len == 0 || len >= out_len)
    return false;
  memcpy(out, type, len);
  out[len] = '\0';
  return true;
}

static bool eip712_parent_array_type(const char *type, char *out,
                                     size_t out_len) {
  if (!type || !out || out_len == 0)
    return false;
  const char *bracket = strrchr(type, '[');
  if (!bracket || bracket == type)
    return false;
  size_t len = (size_t)(bracket - type);
  if (len >= out_len)
    return false;
  memcpy(out, type, len);
  out[len] = '\0';
  return true;
}

static bool eip712_parse_uint_suffix(const char *text, int *bits_out) {
  if (!text || !bits_out)
    return false;
  if (!*text) {
    *bits_out = 256;
    return true;
  }
  int bits = 0;
  for (const char *p = text; *p; p++) {
    if (!isdigit((unsigned char)*p))
      return false;
    bits = bits * 10 + (*p - '0');
  }
  if (bits < 8 || bits > 256 || (bits % 8) != 0)
    return false;
  *bits_out = bits;
  return true;
}

static bool eip712_fixed_bytes_size(const char *type, int *size_out) {
  if (!type || strncmp(type, "bytes", 5) != 0 || !type[5])
    return false;
  int n = 0;
  for (const char *p = type + 5; *p; p++) {
    if (!isdigit((unsigned char)*p))
      return false;
    n = n * 10 + (*p - '0');
  }
  if (n < 1 || n > 32)
    return false;
  if (size_out)
    *size_out = n;
  return true;
}

static bool eip712_is_solidity_type(const char *type) {
  if (!type)
    return false;
  if (strcmp(type, "bool") == 0 || strcmp(type, "address") == 0 ||
      strcmp(type, "string") == 0 || strcmp(type, "bytes") == 0)
    return true;
  int ignored = 0;
  if (strncmp(type, "uint", 4) == 0)
    return eip712_parse_uint_suffix(type + 4, &ignored);
  if (strncmp(type, "int", 3) == 0)
    return eip712_parse_uint_suffix(type + 3, &ignored);
  return eip712_fixed_bytes_size(type, NULL);
}

static const cJSON *eip712_type_fields(const cJSON *types,
                                       const char *type_name) {
  const cJSON *fields = cJSON_GetObjectItemCaseSensitive((cJSON *)types,
                                                         type_name);
  return cJSON_IsArray(fields) ? fields : NULL;
}

static bool eip712_type_list_contains(const eip712_type_list_t *list,
                                      const char *name) {
  if (!list || !name)
    return false;
  for (size_t i = 0; i < list->count; i++) {
    if (strcmp(list->names[i], name) == 0)
      return true;
  }
  return false;
}

static bool eip712_type_list_add(eip712_ctx_t *ctx, eip712_type_list_t *list,
                                 const char *name) {
  if (!list || !name)
    return false;
  if (eip712_type_list_contains(list, name))
    return true;
  if (list->count >= EIP712_MAX_TYPES) {
    eip712_set_error(
        ctx, i18n_tr_or("eip712.error.too_many_types",
                        "Too many TypedData types"));
    return false;
  }
  snprintf(list->names[list->count], sizeof(list->names[list->count]), "%s",
           name);
  list->count++;
  return true;
}

static int eip712_type_name_cmp(const void *a, const void *b) {
  const char *sa = (const char *)a;
  const char *sb = (const char *)b;
  return strcmp(sa, sb);
}

static bool eip712_find_dependencies(eip712_ctx_t *ctx, const cJSON *types,
                                     const char *type_name,
                                     eip712_type_list_t *results) {
  char core[EIP712_MAX_TYPE_NAME];
  if (!eip712_core_type(type_name, core, sizeof(core))) {
    eip712_set_error(ctx, i18n_tr_or("eip712.error.invalid_type_name",
                                     "Invalid type name"));
    return false;
  }
  if (eip712_is_solidity_type(core) || eip712_type_list_contains(results, core))
    return true;

  const cJSON *fields = eip712_type_fields(types, core);
  if (!fields) {
    eip712_set_error(ctx,
                     i18n_tr_or("eip712.error.missing_type_definition_format",
                                "Missing type definition: %s"),
                     core);
    return false;
  }
  if (!eip712_type_list_add(ctx, results, core))
    return false;

  const cJSON *field = NULL;
  cJSON_ArrayForEach(field, fields) {
    const char *child_type =
        eip712_json_string(cJSON_GetObjectItemCaseSensitive((cJSON *)field,
                                                            "type"));
    if (!child_type || !eip712_find_dependencies(ctx, types, child_type,
                                                 results)) {
      if (!ctx->err || !ctx->err[0])
        eip712_set_error(ctx, i18n_tr_or("eip712.error.invalid_type_field",
                                         "Invalid type field"));
      return false;
    }
  }
  return true;
}

static bool eip712_encode_type(eip712_ctx_t *ctx, const cJSON *types,
                               const char *type_name, char **out) {
  if (!out)
    return false;
  *out = NULL;

  eip712_type_list_t deps = {0};
  if (!eip712_find_dependencies(ctx, types, type_name, &deps))
    return false;

  eip712_type_list_t sorted = {0};
  char primary[EIP712_MAX_TYPE_NAME];
  if (!eip712_core_type(type_name, primary, sizeof(primary)))
    return false;
  for (size_t i = 0; i < deps.count; i++) {
    if (strcmp(deps.names[i], primary) != 0)
      snprintf(sorted.names[sorted.count++], sizeof(sorted.names[0]), "%s",
               deps.names[i]);
  }
  qsort(sorted.names, sorted.count, sizeof(sorted.names[0]),
        eip712_type_name_cmp);

  eip712_strbuf_t sb = {0};
  const char *order[EIP712_MAX_TYPES + 1];
  size_t order_count = 0;
  order[order_count++] = primary;
  for (size_t i = 0; i < sorted.count; i++)
    order[order_count++] = sorted.names[i];

  for (size_t i = 0; i < order_count; i++) {
    const cJSON *fields = eip712_type_fields(types, order[i]);
    if (!fields) {
      eip712_set_error(ctx,
                       i18n_tr_or("eip712.error.missing_type_definition_format",
                                  "Missing type definition: %s"),
                       order[i]);
      free(sb.buf);
      return false;
    }
    if (!eip712_sb_append(&sb, order[i]) || !eip712_sb_append(&sb, "(")) {
      free(sb.buf);
      return false;
    }
    const cJSON *field = NULL;
    bool first = true;
    cJSON_ArrayForEach(field, fields) {
      const char *name =
          eip712_json_string(cJSON_GetObjectItemCaseSensitive((cJSON *)field,
                                                              "name"));
      const char *type =
          eip712_json_string(cJSON_GetObjectItemCaseSensitive((cJSON *)field,
                                                              "type"));
      if (!name || !type) {
        eip712_set_error(
            ctx, i18n_tr_or("eip712.error.type_field_missing_name_type",
                            "Type field is missing name/type"));
        free(sb.buf);
        return false;
      }
      if (!first && !eip712_sb_append(&sb, ",")) {
        free(sb.buf);
        return false;
      }
      first = false;
      if (!eip712_sb_append(&sb, type) || !eip712_sb_append(&sb, " ") ||
          !eip712_sb_append(&sb, name)) {
        free(sb.buf);
        return false;
      }
    }
    if (!eip712_sb_append(&sb, ")")) {
      free(sb.buf);
      return false;
    }
  }

  *out = eip712_sb_take(&sb);
  return *out != NULL;
}

static void eip712_hash_text(const char *text, uint8_t out[32]) {
  evm_keccak256((const uint8_t *)text, text ? strlen(text) : 0, out);
}

static bool eip712_hex_value(char c, uint8_t *out) {
  if (!out)
    return false;
  if (c >= '0' && c <= '9') {
    *out = (uint8_t)(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    *out = (uint8_t)(c - 'a' + 10);
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    *out = (uint8_t)(c - 'A' + 10);
    return true;
  }
  return false;
}

static bool eip712_is_hex_string(const char *text) {
  if (!text || !(text[0] == '0' && (text[1] == 'x' || text[1] == 'X')))
    return false;
  for (const char *p = text + 2; *p; p++) {
    uint8_t ignored = 0;
    if (!eip712_hex_value(*p, &ignored))
      return false;
  }
  return true;
}

static bool eip712_hex_to_alloc(const char *hex, uint8_t **out,
                                size_t *out_len) {
  if (!hex || !out || !out_len || !eip712_is_hex_string(hex))
    return false;
  *out = NULL;
  *out_len = 0;
  size_t total_len = strlen(hex);
  if (total_len < 2)
    return false;
  size_t nibbles = total_len - 2U;
  size_t len = (nibbles + 1U) / 2U;
  uint8_t *buf = len ? calloc(len, 1) : calloc(1, 1);
  if (!buf)
    return false;
  size_t src = nibbles;
  size_t dst = len;
  while (src > 0 && dst > 0) {
    uint8_t lo = 0;
    if (!eip712_hex_value(hex[2U + --src], &lo)) {
      free(buf);
      return false;
    }
    uint8_t hi = 0;
    if (src > 0) {
      if (!eip712_hex_value(hex[2U + --src], &hi)) {
        free(buf);
        return false;
      }
    }
    buf[--dst] = (uint8_t)((hi << 4) | lo);
  }
  *out = buf;
  *out_len = len;
  return true;
}

static bool eip712_json_bytes_alloc(const cJSON *value, uint8_t **out,
                                    size_t *out_len) {
  if (!out || !out_len)
    return false;
  *out = NULL;
  *out_len = 0;
  if (cJSON_IsString(value)) {
    const char *text = value->valuestring ? value->valuestring : "";
    if (eip712_is_hex_string(text))
      return eip712_hex_to_alloc(text, out, out_len);
    size_t len = strlen(text);
    uint8_t *buf = len ? malloc(len) : malloc(1);
    if (!buf)
      return false;
    if (len)
      memcpy(buf, text, len);
    *out = buf;
    *out_len = len;
    return true;
  }
  if (cJSON_IsNumber(value)) {
    double d = value->valuedouble;
    if (d < 0)
      d = 0;
    uint64_t n = (uint64_t)d;
    uint8_t tmp[8];
    size_t len = 0;
    bool seen = false;
    for (int i = 7; i >= 0; i--) {
      uint8_t b = (uint8_t)(n >> (unsigned)(i * 8));
      if (b || seen || i == 0) {
        seen = true;
        tmp[len++] = b;
      }
    }
    uint8_t *buf = malloc(len);
    if (!buf)
      return false;
    memcpy(buf, tmp, len);
    *out = buf;
    *out_len = len;
    return true;
  }
  return false;
}

static bool eip712_parse_uint256_string(const char *text, uint8_t out[32],
                                        bool *negative_out) {
  if (!text || !out)
    return false;
  memset(out, 0, 32);
  if (negative_out)
    *negative_out = false;
  while (isspace((unsigned char)*text))
    text++;
  if (*text == '-') {
    if (negative_out)
      *negative_out = true;
    text++;
  } else if (*text == '+') {
    text++;
  }
  while (isspace((unsigned char)*text))
    text++;

  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    text += 2;
    while (*text == '0')
      text++;
    size_t hex_len = strlen(text);
    while (hex_len > 0 && isspace((unsigned char)text[hex_len - 1]))
      hex_len--;
    if (hex_len > 64)
      return false;
    for (size_t i = 0; i < hex_len; i++) {
      uint8_t v = 0;
      if (!eip712_hex_value(text[hex_len - 1 - i], &v))
        return false;
      if ((i & 1U) == 0)
        out[31 - (i / 2U)] |= v;
      else
        out[31 - (i / 2U)] |= (uint8_t)(v << 4);
    }
    return true;
  }

  bool any = false;
  for (const char *p = text; *p; p++) {
    if (isspace((unsigned char)*p))
      break;
    if (!isdigit((unsigned char)*p))
      return false;
    any = true;
    uint16_t carry = (uint16_t)(*p - '0');
    for (int i = 31; i >= 0; i--) {
      uint16_t v = (uint16_t)out[i] * 10U + carry;
      out[i] = (uint8_t)(v & 0xffU);
      carry = (uint16_t)(v >> 8);
    }
    if (carry)
      return false;
  }
  return any;
}

static bool eip712_parse_json_integer(const cJSON *value, uint8_t out[32],
                                      bool signed_type, eip712_ctx_t *ctx) {
  bool negative = false;
  bool ok = false;
  if (cJSON_IsString(value)) {
    ok = eip712_parse_uint256_string(value->valuestring, out, &negative);
  } else if (cJSON_IsNumber(value)) {
    char text[64];
    snprintf(text, sizeof(text), "%.0f", value->valuedouble);
    ok = eip712_parse_uint256_string(text, out, &negative);
  }
  if (!ok) {
    eip712_set_error(ctx, i18n_tr_or("eip712.error.invalid_integer",
                                     "Invalid integer format"));
    return false;
  }
  if (negative && !signed_type) {
    eip712_set_error(ctx, i18n_tr_or("eip712.error.uint_negative",
                                     "uint cannot be negative"));
    return false;
  }
  if (negative) {
    for (size_t i = 0; i < 32; i++)
      out[i] = (uint8_t)~out[i];
    for (int i = 31; i >= 0; i--) {
      out[i]++;
      if (out[i] != 0)
        break;
    }
  }
  return true;
}

static bool eip712_parse_address(const cJSON *value, uint8_t out[32]) {
  if (!cJSON_IsString(value) || !value->valuestring)
    return false;
  const char *text = value->valuestring;
  if (!(text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) ||
      strlen(text + 2) != 40)
    return false;
  memset(out, 0, 32);
  for (size_t i = 0; i < 20; i++) {
    uint8_t hi = 0, lo = 0;
    if (!eip712_hex_value(text[2 + i * 2], &hi) ||
        !eip712_hex_value(text[3 + i * 2], &lo))
      return false;
    out[12 + i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

static bool eip712_encode_field(eip712_ctx_t *ctx, const cJSON *types,
                                const char *name, const char *type,
                                const cJSON *value, uint8_t out[32]);

static bool eip712_hash_struct(eip712_ctx_t *ctx, const cJSON *types,
                               const char *type_name, const cJSON *data,
                               uint8_t out[32]) {
  if (!cJSON_IsObject(data)) {
    eip712_set_error(ctx,
                     i18n_tr_or("eip712.error.data_not_object_format",
                                "%s data is not an object"),
                     type_name ? type_name
                               : i18n_tr_or("eip712.label.message",
                                            "message"));
    return false;
  }
  const cJSON *fields = eip712_type_fields(types, type_name);
  if (!fields) {
    eip712_set_error(ctx,
                     i18n_tr_or("eip712.error.missing_type_definition_format",
                                "Missing type definition: %s"),
                     type_name);
    return false;
  }

  char *encoded_type = NULL;
  if (!eip712_encode_type(ctx, types, type_name, &encoded_type))
    return false;

  size_t field_count = cJSON_GetArraySize((cJSON *)fields);
  uint8_t *encoded = calloc(field_count + 1U, 32);
  if (!encoded) {
    free(encoded_type);
    eip712_set_error(ctx, i18n_tr_or("error.out_of_memory", "Out of memory"));
    return false;
  }
  eip712_hash_text(encoded_type, encoded);
  free(encoded_type);

  size_t slot = 1;
  const cJSON *field = NULL;
  cJSON_ArrayForEach(field, fields) {
    const char *field_name =
        eip712_json_string(cJSON_GetObjectItemCaseSensitive((cJSON *)field,
                                                            "name"));
    const char *field_type =
        eip712_json_string(cJSON_GetObjectItemCaseSensitive((cJSON *)field,
                                                            "type"));
    if (!field_name || !field_type ||
        !eip712_encode_field(ctx, types, field_name, field_type,
                             cJSON_GetObjectItemCaseSensitive((cJSON *)data,
                                                              field_name),
                             encoded + slot * 32U)) {
      free(encoded);
      if (!ctx->err || !ctx->err[0])
        eip712_set_error(ctx, i18n_tr_or("eip712.error.field_encode_failed",
                                         "Field encoding failed"));
      return false;
    }
    slot++;
  }

  evm_keccak256(encoded, (field_count + 1U) * 32U, out);
  free(encoded);
  return true;
}

static bool eip712_encode_array(eip712_ctx_t *ctx, const cJSON *types,
                                const char *name, const char *type,
                                const cJSON *value, uint8_t out[32]) {
  if (!cJSON_IsArray(value)) {
    eip712_set_error(ctx,
                     i18n_tr_or("eip712.error.requires_array_format",
                                "%s requires an array"),
                     name ? name : i18n_tr_or("eip712.label.field", "field"));
    return false;
  }
  char parent_type[EIP712_MAX_TYPE_NAME];
  if (!eip712_parent_array_type(type, parent_type, sizeof(parent_type))) {
    eip712_set_error(ctx, i18n_tr_or("eip712.error.invalid_array_type",
                                     "Invalid array type"));
    return false;
  }
  size_t count = cJSON_GetArraySize((cJSON *)value);
  if (count == 0) {
    evm_keccak256(NULL, 0, out);
    return true;
  }
  uint8_t *encoded = calloc(count, 32);
  if (!encoded) {
    eip712_set_error(ctx, i18n_tr_or("error.out_of_memory", "Out of memory"));
    return false;
  }
  const cJSON *item = NULL;
  size_t index = 0;
  cJSON_ArrayForEach(item, value) {
    if (!eip712_encode_field(ctx, types, name, parent_type, item,
                             encoded + index * 32U)) {
      free(encoded);
      return false;
    }
    index++;
  }
  evm_keccak256(encoded, count * 32U, out);
  free(encoded);
  return true;
}

static bool eip712_encode_field(eip712_ctx_t *ctx, const cJSON *types,
                                const char *name, const char *type,
                                const cJSON *value, uint8_t out[32]) {
  if (!ctx || !type || !out)
    return false;
  memset(out, 0, 32);

  if (eip712_is_array_type(type))
    return eip712_encode_array(ctx, types, name, type, value, out);

  if (types && eip712_type_fields(types, type)) {
    if (!value || cJSON_IsNull(value))
      return true;
    return eip712_hash_struct(ctx, types, type, value, out);
  }

  if ((!value || cJSON_IsNull(value)) &&
      (strcmp(type, "string") == 0 || strcmp(type, "bytes") == 0)) {
    return true;
  }
  if (!value || cJSON_IsNull(value)) {
    eip712_set_error(ctx,
                     i18n_tr_or("eip712.error.missing_field_format",
                                "Missing field: %s"),
                     name ? name : "-");
    return false;
  }

  if (strcmp(type, "bool") == 0) {
    bool truthy = false;
    if (cJSON_IsBool(value)) {
      truthy = cJSON_IsTrue(value);
    } else if (cJSON_IsString(value)) {
      const char *s = value->valuestring ? value->valuestring : "";
      truthy = !(strcmp(s, "False") == 0 || strcmp(s, "false") == 0 ||
                 strcmp(s, "0") == 0 || s[0] == '\0');
    } else if (cJSON_IsNumber(value)) {
      truthy = value->valuedouble != 0;
    }
    out[31] = truthy ? 1 : 0;
    return true;
  }

  if (strcmp(type, "address") == 0) {
    if (!eip712_parse_address(value, out)) {
      eip712_set_error(ctx,
                       i18n_tr_or("eip712.error.invalid_address_format",
                                  "Invalid address format: %s"),
                       name ? name : "-");
      return false;
    }
    return true;
  }

  if (strcmp(type, "string") == 0) {
    uint8_t *bytes = NULL;
    size_t len = 0;
    if (cJSON_IsString(value)) {
      const char *text = value->valuestring ? value->valuestring : "";
      evm_keccak256((const uint8_t *)text, strlen(text), out);
      return true;
    }
    if (!eip712_json_bytes_alloc(value, &bytes, &len)) {
      eip712_set_error(ctx, i18n_tr_or("eip712.error.invalid_string_field",
                                       "Invalid string field"));
      return false;
    }
    evm_keccak256(bytes, len, out);
    free(bytes);
    return true;
  }

  if (strcmp(type, "bytes") == 0) {
    uint8_t *bytes = NULL;
    size_t len = 0;
    if (!eip712_json_bytes_alloc(value, &bytes, &len)) {
      eip712_set_error(ctx, i18n_tr_or("eip712.error.invalid_bytes_field",
                                       "Invalid bytes field"));
      return false;
    }
    evm_keccak256(bytes, len, out);
    free(bytes);
    return true;
  }

  int fixed_bytes = 0;
  if (eip712_fixed_bytes_size(type, &fixed_bytes)) {
    uint8_t *bytes = NULL;
    size_t len = 0;
    if (!eip712_json_bytes_alloc(value, &bytes, &len) ||
        len > (size_t)fixed_bytes) {
      free(bytes);
      eip712_set_error(ctx,
                       i18n_tr_or("eip712.error.invalid_length_format",
                                  "%s length is invalid"),
                       type);
      return false;
    }
    memcpy(out, bytes, len);
    free(bytes);
    return true;
  }

  if (strncmp(type, "uint", 4) == 0) {
    int bits = 0;
    if (!eip712_parse_uint_suffix(type + 4, &bits) ||
        !eip712_parse_json_integer(value, out, false, ctx))
      return false;
    (void)bits;
    return true;
  }

  if (strncmp(type, "int", 3) == 0) {
    int bits = 0;
    if (!eip712_parse_uint_suffix(type + 3, &bits) ||
        !eip712_parse_json_integer(value, out, true, ctx))
      return false;
    (void)bits;
    return true;
  }

  eip712_set_error(ctx,
                   i18n_tr_or("eip712.error.unsupported_field_type_format",
                              "Unsupported field type: %s"),
                   type);
  return false;
}

static bool eip712_domain_key_allowed(const char *key, const char **type_out) {
  static const struct {
    const char *name;
    const char *type;
  } allowed[] = {
      {"name", "string"},
      {"version", "string"},
      {"chainId", "uint256"},
      {"verifyingContract", "address"},
      {"salt", "bytes32"},
  };
  for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
    if (strcmp(key, allowed[i].name) == 0) {
      if (type_out)
        *type_out = allowed[i].type;
      return true;
    }
  }
  return false;
}

static bool eip712_domain_matches_declared_type(eip712_ctx_t *ctx,
                                                const cJSON *types,
                                                const cJSON *domain) {
  const cJSON *domain_type = eip712_type_fields(types, "EIP712Domain");
  if (!domain_type)
    return true;

  const cJSON *item = NULL;
  cJSON_ArrayForEach(item, domain) {
    bool found = false;
    const cJSON *field = NULL;
    cJSON_ArrayForEach(field, domain_type) {
      const char *name =
          eip712_json_string(cJSON_GetObjectItemCaseSensitive((cJSON *)field,
                                                              "name"));
      if (name && strcmp(name, item->string) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      eip712_set_error(
          ctx, i18n_tr_or("eip712.error.domain_mismatch",
                          "Domain fields do not match EIP712Domain"));
      return false;
    }
  }

  const cJSON *field = NULL;
  cJSON_ArrayForEach(field, domain_type) {
    const char *name =
        eip712_json_string(cJSON_GetObjectItemCaseSensitive((cJSON *)field,
                                                            "name"));
    if (name && !cJSON_GetObjectItemCaseSensitive((cJSON *)domain, name)) {
      eip712_set_error(
          ctx, i18n_tr_or("eip712.error.domain_mismatch",
                          "Domain fields do not match EIP712Domain"));
      return false;
    }
  }
  return true;
}

static bool eip712_hash_domain(eip712_ctx_t *ctx, const cJSON *domain,
                               uint8_t out[32]) {
  if (!cJSON_IsObject(domain)) {
    eip712_set_error(ctx, i18n_tr_or("eip712.error.domain_not_object",
                                     "Domain is not an object"));
    return false;
  }

  static const char *order[] = {"name", "version", "chainId",
                                "verifyingContract", "salt"};
  size_t count = 0;
  const char *field_types[5] = {0};
  const char *field_names[5] = {0};
  for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive((cJSON *)domain,
                                                          order[i]);
    if (!value)
      continue;
    const char *field_type = NULL;
    if (!eip712_domain_key_allowed(order[i], &field_type)) {
      return false;
    }
    field_names[count] = order[i];
    field_types[count] = field_type;
    count++;
  }

  const cJSON *item = NULL;
  cJSON_ArrayForEach(item, domain) {
    if (!eip712_domain_key_allowed(item->string, NULL)) {
      eip712_set_error(ctx,
                       i18n_tr_or("eip712.error.invalid_domain_field_format",
                                  "Invalid domain field: %s"),
                       item->string);
      return false;
    }
  }

  eip712_strbuf_t sb = {0};
  if (!eip712_sb_append(&sb, "EIP712Domain(")) {
    free(sb.buf);
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    if (i > 0 && !eip712_sb_append(&sb, ",")) {
      free(sb.buf);
      return false;
    }
    if (!eip712_sb_append(&sb, field_types[i]) ||
        !eip712_sb_append(&sb, " ") ||
        !eip712_sb_append(&sb, field_names[i])) {
      free(sb.buf);
      return false;
    }
  }
  if (!eip712_sb_append(&sb, ")")) {
    free(sb.buf);
    return false;
  }

  uint8_t *encoded = calloc(count + 1U, 32);
  if (!encoded) {
    free(sb.buf);
    eip712_set_error(ctx, i18n_tr_or("error.out_of_memory", "Out of memory"));
    return false;
  }
  eip712_hash_text(sb.buf, encoded);
  free(sb.buf);

  for (size_t i = 0; i < count; i++) {
    if (!eip712_encode_field(ctx, NULL, field_names[i], field_types[i],
                             cJSON_GetObjectItemCaseSensitive((cJSON *)domain,
                                                              field_names[i]),
                             encoded + (i + 1U) * 32U)) {
      free(encoded);
      return false;
    }
  }

  evm_keccak256(encoded, (count + 1U) * 32U, out);
  free(encoded);
  return true;
}

static bool eip712_get_primary_type(eip712_ctx_t *ctx, const cJSON *types,
                                    char *out, size_t out_len) {
  if (!types || !out || out_len == 0)
    return false;
  out[0] = '\0';

  eip712_type_list_t custom = {0};
  eip712_type_list_t deps = {0};
  const cJSON *type = NULL;
  cJSON_ArrayForEach(type, types) {
    if (!type->string || strcmp(type->string, "EIP712Domain") == 0)
      continue;
    if (!cJSON_IsArray(type))
      continue;
    if (!eip712_type_list_add(ctx, &custom, type->string))
      return false;
  }
  if (custom.count == 0) {
    eip712_set_error(ctx,
                     i18n_tr_or("eip712.error.missing_custom_message_type",
                                "Missing custom message type"));
    return false;
  }

  for (size_t i = 0; i < custom.count; i++) {
    const cJSON *fields = eip712_type_fields(types, custom.names[i]);
    const cJSON *field = NULL;
    cJSON_ArrayForEach(field, fields) {
      const char *field_type =
          eip712_json_string(cJSON_GetObjectItemCaseSensitive((cJSON *)field,
                                                              "type"));
      char core[EIP712_MAX_TYPE_NAME];
      if (!field_type || !eip712_core_type(field_type, core, sizeof(core)))
        continue;
      if (eip712_type_list_contains(&custom, core) &&
          strcmp(core, custom.names[i]) != 0) {
        if (!eip712_type_list_add(ctx, &deps, core))
          return false;
      }
    }
  }

  size_t candidates = 0;
  for (size_t i = 0; i < custom.count; i++) {
    if (!eip712_type_list_contains(&deps, custom.names[i])) {
      snprintf(out, out_len, "%s", custom.names[i]);
      candidates++;
    }
  }
  if (candidates != 1) {
    eip712_set_error(ctx,
                     i18n_tr_or("eip712.error.primary_type_undetermined",
                                "Unable to determine primaryType"));
    out[0] = '\0';
    return false;
  }
  return true;
}

bool eip712_hash_typed_data_json(const char *typed_data_json, uint8_t out[32],
                                 char *err, size_t err_len) {
  if (err && err_len > 0)
    err[0] = '\0';
  if (!typed_data_json || !out) {
    if (err && err_len > 0)
      snprintf(err, err_len, "%s",
               i18n_tr_or("eip712.error.typed_data_empty",
                          "TypedData is empty"));
    return false;
  }
  eip712_ctx_t ctx = {.err = err, .err_len = err_len};

  cJSON *root = cJSON_Parse(typed_data_json);
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    eip712_set_error(&ctx,
                     i18n_tr_or("eip712.error.typed_data_not_object",
                                "TypedData is not a JSON object"));
    return false;
  }

  const cJSON *types = cJSON_GetObjectItemCaseSensitive(root, "types");
  const cJSON *domain = cJSON_GetObjectItemCaseSensitive(root, "domain");
  const cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
  const char *provided_primary =
      eip712_json_string(cJSON_GetObjectItemCaseSensitive(root,
                                                          "primaryType"));
  if (!cJSON_IsObject(types) || !cJSON_IsObject(message)) {
    eip712_set_error(&ctx,
                     i18n_tr_or("eip712.error.typed_data_missing_types_message",
                                "TypedData is missing types/message"));
    cJSON_Delete(root);
    return false;
  }
  if (!domain)
    domain = cJSON_CreateObject();
  if (!cJSON_IsObject(domain)) {
    eip712_set_error(&ctx, i18n_tr_or("eip712.error.domain_not_object",
                                      "Domain is not an object"));
    if (domain && domain->string == NULL)
      cJSON_Delete((cJSON *)domain);
    cJSON_Delete(root);
    return false;
  }

  if (!eip712_domain_matches_declared_type(&ctx, types, domain)) {
    if (domain && domain->string == NULL)
      cJSON_Delete((cJSON *)domain);
    cJSON_Delete(root);
    return false;
  }

  char primary[EIP712_MAX_TYPE_NAME] = {0};
  if (!eip712_get_primary_type(&ctx, types, primary, sizeof(primary))) {
    if (domain && domain->string == NULL)
      cJSON_Delete((cJSON *)domain);
    cJSON_Delete(root);
    return false;
  }
  if (provided_primary && strcmp(provided_primary, primary) != 0) {
    eip712_set_error(
        &ctx,
        i18n_tr_or("eip712.error.primary_type_mismatch",
                   "primaryType does not match the type definitions"));
    if (domain && domain->string == NULL)
      cJSON_Delete((cJSON *)domain);
    cJSON_Delete(root);
    return false;
  }

  uint8_t domain_hash[32];
  uint8_t message_hash[32];
  bool ok = eip712_hash_domain(&ctx, domain, domain_hash) &&
            eip712_hash_struct(&ctx, types, primary, message, message_hash);
  if (domain && domain->string == NULL)
    cJSON_Delete((cJSON *)domain);
  cJSON_Delete(root);
  if (!ok)
    return false;

  uint8_t final_data[66];
  final_data[0] = 0x19;
  final_data[1] = 0x01;
  memcpy(final_data + 2, domain_hash, 32);
  memcpy(final_data + 34, message_hash, 32);
  evm_keccak256(final_data, sizeof(final_data), out);
  memset(domain_hash, 0, sizeof(domain_hash));
  memset(message_hash, 0, sizeof(message_hash));
  memset(final_data, 0, sizeof(final_data));
  return true;
}
