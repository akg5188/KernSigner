#include "i18n.h"

#include <string.h>

static i18n_language_t s_current_language = I18N_LANG_EN;

static const i18n_language_info_t LANGUAGE_TABLE[] = {
    {I18N_LANG_EN, "en", "English", "English", false},
    {I18N_LANG_ZH_HANS_CN, "zh_Hans_CN", "Simplified Chinese", "简体中文",
     false},
};

const i18n_language_info_t *i18n_languages(void) { return LANGUAGE_TABLE; }

size_t i18n_language_count(void) {
  return sizeof(LANGUAGE_TABLE) / sizeof(LANGUAGE_TABLE[0]);
}

bool i18n_language_valid(i18n_language_t language) {
  return language == I18N_LANG_EN || language == I18N_LANG_ZH_HANS_CN;
}

const i18n_language_info_t *i18n_language_info(i18n_language_t language) {
  for (size_t i = 0; i < i18n_language_count(); i++) {
    if (LANGUAGE_TABLE[i].id == language)
      return &LANGUAGE_TABLE[i];
  }
  return &LANGUAGE_TABLE[0];
}

i18n_language_t i18n_language_from_code(const char *code) {
  if (!code)
    return I18N_LANG_EN;
  if (strcmp(code, "zh") == 0 || strcmp(code, "zh_CN") == 0 ||
      strcmp(code, "zh_Hans_CN") == 0)
    return I18N_LANG_ZH_HANS_CN;
  return I18N_LANG_EN;
}

void i18n_set_language(i18n_language_t language) {
  s_current_language = i18n_language_valid(language) ? language : I18N_LANG_EN;
}

i18n_language_t i18n_get_language(void) { return s_current_language; }

const char *i18n_tr_or(const char *key, const char *fallback) {
  if (fallback && fallback[0])
    return fallback;
  return key ? key : "";
}

const char *i18n_tr(const char *key) { return i18n_tr_or(key, key); }
