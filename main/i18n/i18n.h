#ifndef KSIG_I18N_H
#define KSIG_I18N_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  I18N_LANG_EN = 0,
  I18N_LANG_CA = 1,
  I18N_LANG_CS = 2,
  I18N_LANG_DE = 3,
  I18N_LANG_EL = 4,
  I18N_LANG_ES = 5,
  I18N_LANG_FA = 6,
  I18N_LANG_FR = 7,
  I18N_LANG_HI = 8,
  I18N_LANG_ID = 9,
  I18N_LANG_IT = 10,
  I18N_LANG_JA = 11,
  I18N_LANG_KO = 12,
  I18N_LANG_NL = 13,
  I18N_LANG_PL = 14,
  I18N_LANG_PT_BR = 15,
  I18N_LANG_RU = 16,
  I18N_LANG_TH = 17,
  I18N_LANG_TR = 18,
  I18N_LANG_VI = 19,
  I18N_LANG_ZH_HANS_CN = 20,
  I18N_LANG_COUNT,
} i18n_language_t;

typedef struct {
  i18n_language_t id;
  const char *code;
  const char *english_name;
  const char *native_name;
  bool rtl;
} i18n_language_info_t;

const i18n_language_info_t *i18n_languages(void);
const i18n_language_info_t *i18n_language_info(i18n_language_t language);
size_t i18n_language_count(void);
bool i18n_language_valid(i18n_language_t language);
i18n_language_t i18n_language_from_code(const char *code);
void i18n_set_language(i18n_language_t language);
i18n_language_t i18n_get_language(void);
const char *i18n_tr(const char *key);
const char *i18n_tr_or(const char *key, const char *fallback);

#endif // KSIG_I18N_H
