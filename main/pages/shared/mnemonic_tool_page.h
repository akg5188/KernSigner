#ifndef MNEMONIC_TOOL_PAGE_H
#define MNEMONIC_TOOL_PAGE_H

#include <lvgl.h>

typedef enum {
  MNEMONIC_TOOL_INDEX_IMPORT,
  MNEMONIC_TOOL_HEX_ENTROPY,
  MNEMONIC_TOOL_BINARY_ENTROPY,
  MNEMONIC_TOOL_D6_ENTROPY,
  MNEMONIC_TOOL_D20_ENTROPY,
  MNEMONIC_TOOL_CARD_ENTROPY,
  MNEMONIC_TOOL_BIP85_MNEMONIC,
  MNEMONIC_TOOL_XOR_CURRENT,
  MNEMONIC_TOOL_SECONDARY_SHIFT,
  MNEMONIC_TOOL_SECONDARY_ADD,
  MNEMONIC_TOOL_SECONDARY_SUB,
  MNEMONIC_TOOL_STEEL_RESTORE,
} mnemonic_tool_mode_t;

void mnemonic_tool_page_create(lv_obj_t *parent, void (*return_cb)(void),
                               mnemonic_tool_mode_t mode);
void mnemonic_tool_page_show(void);
void mnemonic_tool_page_hide(void);
void mnemonic_tool_page_destroy(void);
char *mnemonic_tool_page_get_completed_mnemonic(void);

#endif // MNEMONIC_TOOL_PAGE_H
