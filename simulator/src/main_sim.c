/**
 * Kern Desktop Simulator — Entry Point
 *
 * Mirrors the initialization sequence from main/main.c but uses
 * SDL2 for display and mouse input instead of ESP32-P4 hardware.
 */

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "ui/theme.h"
#include "core/settings.h"
#include "pages/krux_shell/krux_shell.h"
#include "krux_port/krux_feature_catalog.h"
#include "esp_lvgl_port.h"
#include <nvs_flash.h>
#include <esp_err.h>
#include "sim_video.h"
#include "sim_nvs.h"
#include "sim_sdcard.h"
#include <bsp/pmic.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#include <sys/stat.h>
#include "esp_log.h"

#ifndef SIM_LCD_H_RES
#define SIM_LCD_H_RES 720
#endif
#ifndef SIM_LCD_V_RES
#define SIM_LCD_V_RES 720
#endif

static void run_lvgl_frames(int frame_count) {
    for (int i = 0; i < frame_count; i++) {
        lvgl_port_lock(0);
        (void)lv_timer_handler();
        lvgl_port_unlock();
        SDL_Delay(16);
    }
}

static void put_le16(uint8_t *buf, uint16_t v) {
    buf[0] = (uint8_t)(v & 0xff);
    buf[1] = (uint8_t)((v >> 8) & 0xff);
}

static void put_le32(uint8_t *buf, uint32_t v) {
    buf[0] = (uint8_t)(v & 0xff);
    buf[1] = (uint8_t)((v >> 8) & 0xff);
    buf[2] = (uint8_t)((v >> 16) & 0xff);
    buf[3] = (uint8_t)((v >> 24) & 0xff);
}

static int write_screen_bmp(const char *path) {
#if LV_USE_SNAPSHOT
    lv_draw_buf_t *shot =
        lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    if (!shot) {
        fprintf(stderr, "snapshot failed for %s\n", path);
        return 1;
    }

    const uint32_t w = shot->header.w;
    const uint32_t h = shot->header.h;
    const uint32_t row_size = ((w * 3U + 3U) / 4U) * 4U;
    const uint32_t pixel_size = row_size * h;
    const uint32_t file_size = 54U + pixel_size;

    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        lv_draw_buf_destroy(shot);
        return 1;
    }

    uint8_t header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    put_le32(&header[2], file_size);
    put_le32(&header[10], 54);
    put_le32(&header[14], 40);
    put_le32(&header[18], w);
    put_le32(&header[22], h);
    put_le16(&header[26], 1);
    put_le16(&header[28], 24);
    put_le32(&header[34], pixel_size);
    fwrite(header, 1, sizeof(header), f);

    uint8_t pad[3] = {0};
    const uint32_t pad_len = row_size - w * 3U;
    for (int32_t y = (int32_t)h - 1; y >= 0; y--) {
        const uint8_t *src = shot->data + (uint32_t)y * shot->header.stride;
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t r = src[x * 3U + 2U];
            const uint8_t g = src[x * 3U + 1U];
            const uint8_t b = src[x * 3U + 0U];
            fputc(b, f);
            fputc(g, f);
            fputc(r, f);
        }
        fwrite(pad, 1, pad_len, f);
    }

    fclose(f);
    lv_draw_buf_destroy(shot);
    return 0;
#else
    (void)path;
    fprintf(stderr, "LV_USE_SNAPSHOT is disabled\n");
    return 1;
#endif
}

static uint32_t utf8_next_codepoint(const char *text, size_t len,
                                    size_t *index) {
    const unsigned char *s = (const unsigned char *)text;
    size_t i = *index;
    unsigned char c = s[i++];

    if (c < 0x80) {
        *index = i;
        return c;
    }

    uint32_t cp = 0xfffd;
    if ((c & 0xe0) == 0xc0 && i < len && (s[i] & 0xc0) == 0x80) {
        cp = ((uint32_t)(c & 0x1f) << 6) | (uint32_t)(s[i] & 0x3f);
        i += 1;
    } else if ((c & 0xf0) == 0xe0 && i + 1 < len &&
               (s[i] & 0xc0) == 0x80 &&
               (s[i + 1] & 0xc0) == 0x80) {
        cp = ((uint32_t)(c & 0x0f) << 12) |
             ((uint32_t)(s[i] & 0x3f) << 6) |
             (uint32_t)(s[i + 1] & 0x3f);
        i += 2;
    } else if ((c & 0xf8) == 0xf0 && i + 2 < len &&
               (s[i] & 0xc0) == 0x80 &&
               (s[i + 1] & 0xc0) == 0x80 &&
               (s[i + 2] & 0xc0) == 0x80) {
        cp = ((uint32_t)(c & 0x07) << 18) |
             ((uint32_t)(s[i] & 0x3f) << 12) |
             ((uint32_t)(s[i + 1] & 0x3f) << 6) |
             (uint32_t)(s[i + 2] & 0x3f);
        i += 3;
    }

    *index = i;
    return cp;
}

static void count_ui_objects_recursive(lv_obj_t *obj, size_t *labels,
                                       size_t *clickables) {
    if (!obj)
        return;

    if (labels && lv_obj_check_type(obj, &lv_label_class))
        (*labels)++;
    if (clickables && lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE))
        (*clickables)++;

    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, (int32_t)i);
        count_ui_objects_recursive(child, labels, clickables);
    }
}

static bool object_contains_label_text_recursive(lv_obj_t *obj,
                                                 const char *needle) {
    if (!obj || !needle)
        return false;

    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *text = lv_label_get_text(obj);
        if (text && strcmp(text, needle) == 0)
            return true;
    }

    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, (int32_t)i);
        if (object_contains_label_text_recursive(child, needle))
            return true;
    }

    return false;
}

static lv_obj_t *find_clickable_by_label_recursive(lv_obj_t *obj,
                                                   const char *label) {
    if (!obj || !label)
        return NULL;

    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, (int32_t)i);
        lv_obj_t *found = find_clickable_by_label_recursive(child, label);
        if (found)
            return found;
    }

    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE) &&
        object_contains_label_text_recursive(obj, label)) {
        return obj;
    }

    return NULL;
}

static bool click_button_with_label(const char *label) {
    lv_obj_t *button = find_clickable_by_label_recursive(lv_screen_active(), label);
    if (!button)
        return false;

    (void)lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    run_lvgl_frames(10);
    return true;
}

static bool object_contains_label_fragment_recursive(lv_obj_t *obj,
                                                     const char *needle) {
    if (!obj || !needle)
        return false;

    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *text = lv_label_get_text(obj);
        if (text && strstr(text, needle) != NULL)
            return true;
    }

    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, (int32_t)i);
        if (object_contains_label_fragment_recursive(child, needle))
            return true;
    }

    return false;
}

typedef struct {
    const char *start_id;
    const char *button_label;
    const char *expected_id;
    const char *expected_text;
} button_navigation_check_t;

static void write_tsv_field(FILE *file, const char *text) {
    if (!file)
        return;

    for (const char *p = text ? text : ""; *p; p++) {
        switch (*p) {
        case '\t':
            fputs("\\t", file);
            break;
        case '\n':
            fputs("\\n", file);
            break;
        case '\r':
            fputs("\\r", file);
            break;
        default:
            fputc(*p, file);
            break;
        }
    }
}

static int record_button_navigation_check(FILE *interaction_file,
                                          const char *start_id,
                                          const char *button_label,
                                          const char *expected_id,
                                          const char *expected_text) {
    if (!krux_shell_show_screen(start_id)) {
        if (interaction_file) {
            write_tsv_field(interaction_file, start_id);
            fputc('\t', interaction_file);
            write_tsv_field(interaction_file, button_label);
            fputc('\t', interaction_file);
            write_tsv_field(interaction_file, expected_id);
            fputc('\t', interaction_file);
            write_tsv_field(interaction_file, expected_text);
            fputs("\t\tmissing_screen\n", interaction_file);
        }
        return 1;
    }

    run_lvgl_frames(3);
    bool clicked = click_button_with_label(button_label);
    const char *actual_id = krux_shell_current_screen_id();
    bool action_only = expected_id && strcmp(expected_id, "*action*") == 0;
    bool matched_id = clicked && actual_id && expected_id && !action_only &&
                      strcmp(actual_id, expected_id) == 0;
    bool matched_text =
        clicked && expected_text &&
        (object_contains_label_text_recursive(lv_screen_active(), expected_text) ||
         object_contains_label_fragment_recursive(lv_screen_active(),
                                                 expected_text));
    bool ok = clicked &&
              (action_only ? matched_text : (matched_id || matched_text));

    if (interaction_file) {
        write_tsv_field(interaction_file, start_id);
        fputc('\t', interaction_file);
        write_tsv_field(interaction_file, button_label);
        fputc('\t', interaction_file);
        write_tsv_field(interaction_file, expected_id);
        fputc('\t', interaction_file);
        write_tsv_field(interaction_file, expected_text);
        fputc('\t', interaction_file);
        write_tsv_field(interaction_file, actual_id);
        fputc('\t', interaction_file);
        if (ok) {
            if (matched_id) {
                fputs("ok", interaction_file);
            } else if (matched_text) {
                fputs("ok_by_text", interaction_file);
            } else {
                fputs("ok_action", interaction_file);
            }
        } else {
            fputs(clicked ? "wrong_target" : "missing_button",
                  interaction_file);
        }
        fputc('\n', interaction_file);
    }

    return ok ? 0 : 1;
}

static int run_button_interaction_checks(FILE *interaction_file) {
    static const button_navigation_check_t checks[] = {
        {"home", "扫码签名", "sign_psbt_qr", NULL},
        {"home", "助记词", "pi_mnemonic_tools", NULL},
        {"home", "连接钱包", "pi_connect_wallet", NULL},
        {"home", "设置", "settings", NULL},
        {"home", "自检", "pi_self_check", NULL},
        {"home", "工具", "tools", NULL},
        {"tools", "二维码", "tools_create_qr", NULL},
        {"tools", "扫码", "tools_qr_capture", NULL},
        {"tools", "文件", "tools_file_manager", NULL},
        {"pi_mnemonic_tools", "导入", "load_mnemonic", NULL},
        {"pi_mnemonic_tools", "创建", "new_mnemonic", NULL},
        {"pi_mnemonic_tools", "密语", "login_passphrase", NULL},
        {"pi_mnemonic_tools", "高级", "pi_mnemonic_advanced", NULL},
        {"pi_mnemonic_advanced", "派生地址", "custom_derivation", NULL},
        {"pi_mnemonic_advanced", "自检", "bip39_check_tools", NULL},
        {"pi_connect_wallet", "OKX", "connect_okx", NULL},
        {"pi_connect_wallet", "Bitget", "connect_bitget", NULL},
        {"pi_connect_wallet", "MetaMask", "connect_metamask", NULL},
        {"pi_connect_wallet", "Rabby", "connect_rabby", NULL},
        {"pi_connect_wallet", "TokenPocket", "connect_tokenpocket", NULL},
        {"pi_connect_wallet", "地址", "connect_address", NULL},
        {"pi_connect_wallet", "BTC", "btc_wallet", NULL},
        {"connect_okx", "助记词", "web3_okx_mnemonic", NULL},
        {"connect_okx", "智能卡", "web3_okx_satochip", NULL},
        {"connect_bitget", "助记词", "web3_bitget_mnemonic", NULL},
        {"connect_bitget", "智能卡", "web3_bitget_satochip", NULL},
        {"connect_metamask", "助记词", "web3_metamask_mnemonic", NULL},
        {"connect_metamask", "智能卡", "web3_metamask_satochip", NULL},
        {"connect_rabby", "助记词", "web3_rabby_mnemonic", NULL},
        {"connect_rabby", "智能卡", "web3_rabby_satochip", NULL},
        {"connect_tokenpocket", "助记词", "web3_tokenpocket_mnemonic", NULL},
        {"connect_tokenpocket", "智能卡", "web3_tokenpocket_satochip", NULL},
        {"connect_address", "助记词", "web3_address_mnemonic", NULL},
        {"connect_address", "智能卡", "web3_address_satochip", NULL},
        {"btc_wallet", "助记词", "btc_mnemonic", NULL},
        {"btc_wallet", "智能卡", "btc_satochip", NULL},
        {"new_mnemonic", "扑克牌", "new_cards_entropy", NULL},
        {"new_mnemonic", "十六进制", "new_hex_entropy", NULL},
        {"new_mnemonic", "骰子", "new_dice_d6", NULL},
        {"new_mnemonic", "D20", "new_dice_d20", NULL},
        {"new_mnemonic", "手动", "new_words_select", NULL},
        {"new_mnemonic", "相机", "new_camera_entropy", NULL},
        {"new_mnemonic", "智能卡", "new_seedkeeper_create_mnemonic", NULL},
        {"load_mnemonic", "手动", "load_manual", NULL},
        {"load_mnemonic", "序号", "load_digits", NULL},
        {"load_mnemonic", "智能卡", "load_seedkeeper_mnemonic", NULL},
        {"load_mnemonic", "钢板", "load_steel_restore", NULL},
        {"load_mnemonic", "点阵", "load_punch_grid", NULL},
        {"load_punch_grid", "点阵板恢复", "load_tinyseed_restore", NULL},
        {"load_punch_grid", "1248恢复", "load_stackbit_restore", NULL},
        {"load_mnemonic", "二维码", "load_camera", NULL},
        {"load_mnemonic", "存储卡", "load_sd", NULL},
        {"pi_self_check", "系统", "system_overview", NULL},
        {"pi_self_check", "安全", "security_check", NULL},
        {"pi_self_check", "交付", "delivery_check", NULL},
        {"pi_self_check", "智能卡", "smartcard_probe", NULL},
        {"pi_self_check", "外设", "device_tests", NULL},
        {"device_tests", "触摸", "test_screen_touch", NULL},
        {"device_tests", "相机", "test_camera", NULL},
        {"device_tests", "存储卡", "test_storage", NULL},
        {"device_tests", "供电", "test_power", NULL},
        {"backup_export", "序号", "backup_seed_words", NULL},
        {"backup_export", "原始熵", "backup_entropy", NULL},
        {"backup_export", "点阵板", "backup_grid", NULL},
        {"backup_export", "钢板", "backup_steel_punch", NULL},
        {"backup_export", "1248", "backup_stackbit", NULL},
        {"backup_export", "二维码", "backup_seed_qr", NULL},
        {"settings", "PIN", "settings_pin", NULL},
        {"settings", "亮度", "settings_display", NULL},
        {"settings", "相机", "settings_camera", NULL},
        {"settings", "钱包", "settings_wallet", NULL},
        {"settings", "安全", "settings_security", NULL},
        {"settings", "关于", "about", NULL},
    };

    int failures = 0;
    for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
        failures += record_button_navigation_check(interaction_file,
                                                   checks[i].start_id,
                                                   checks[i].button_label,
                                                   checks[i].expected_id,
                                                   checks[i].expected_text);
    }

    for (size_t i = 0; i < krux_shell_screen_count(); i++) {
        const char *id = krux_shell_screen_id_at(i);
        const krux_feature_t *feature = krux_feature_find(id);
        if (!feature || !feature->parent_id)
            continue;

        failures += record_button_navigation_check(interaction_file, feature->id,
                                                   "返回", feature->parent_id,
                                                   NULL);
        const krux_feature_t *parent = krux_feature_find(feature->parent_id);
        if (strcmp(feature->parent_id, "home") != 0 && parent &&
            parent->parent_id && strcmp(parent->parent_id, "home") != 0) {
            failures += record_button_navigation_check(interaction_file,
                                                       feature->id, "回到首页",
                                                       "home", NULL);
        }
    }

    (void)krux_shell_show_screen("home");
    run_lvgl_frames(3);
    return failures;
}

static lv_obj_t *find_scrollable_object_recursive(lv_obj_t *obj) {
    if (!obj)
        return NULL;

    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE) &&
        lv_obj_get_scroll_bottom(obj) > 0) {
        return obj;
    }

    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, (int32_t)i);
        lv_obj_t *found = find_scrollable_object_recursive(child);
        if (found)
            return found;
    }

    return NULL;
}

static size_t check_label_glyphs_recursive(lv_obj_t *obj, FILE *glyph_file,
                                           const char *screen_id) {
    size_t missing = 0;

    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *text = lv_label_get_text(obj);
        const lv_font_t *font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
        size_t text_len = text ? strlen(text) : 0;

        for (size_t i = 0; text && text[i] != '\0';) {
            uint32_t cp = utf8_next_codepoint(text, text_len, &i);
            if (cp == '\n' || cp == '\r' || cp == '\t' || cp < 0x20)
                continue;

            lv_font_glyph_dsc_t dsc;
            bool ok = font && lv_font_get_glyph_dsc(font, &dsc, cp, 0);
            if (!ok) {
                missing++;
                if (glyph_file) {
                    fprintf(glyph_file, "%s\tU+%04X\t%s\n",
                            screen_id ? screen_id : "(null)", cp, text);
                }
            }
        }
    }

    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, (int32_t)i);
        if (child)
            missing += check_label_glyphs_recursive(child, glyph_file, screen_id);
    }

    return missing;
}

static void simulator_capture_current_page(const char *dir, FILE *manifest,
                                           FILE *glyph_file, FILE *smoke_file,
                                           FILE *scroll_file, size_t index,
                                           const char *id,
                                           const char *title,
                                           int *failures) {
    char filename[256];
    char path[512];
    snprintf(filename, sizeof(filename), "%02zu_%s.bmp", index,
             id ? id : "missing");
    snprintf(path, sizeof(path), "%s/%s", dir, filename);

    size_t label_count = 0;
    size_t clickable_count = 0;
    count_ui_objects_recursive(lv_screen_active(), &label_count,
                               &clickable_count);
    const char *smoke_status =
        (label_count > 0 && clickable_count > 0) ? "ok" : "failed";
    if (strcmp(smoke_status, "ok") != 0)
        (*failures)++;
    fprintf(smoke_file, "%zu\t%s\t%zu\t%zu\t%s\n", index,
            id ? id : "", label_count, clickable_count, smoke_status);

    size_t missing_glyphs =
        check_label_glyphs_recursive(lv_screen_active(), glyph_file, id);
    if (missing_glyphs > 0)
        (*failures)++;

    const char *capture_status = "ok";
    if (write_screen_bmp(path) != 0) {
        (*failures)++;
        capture_status = "failed";
    } else {
        printf("screenshot: %s\n", path);
    }

    lv_obj_update_layout(lv_screen_active());
    lv_obj_t *scroll_obj = find_scrollable_object_recursive(lv_screen_active());
    int32_t bottom_before = scroll_obj ? lv_obj_get_scroll_bottom(scroll_obj) : 0;
    int32_t bottom_after = bottom_before;
    char bottom_filename[256] = "";
    const char *scroll_status = scroll_obj ? "ok" : "not_scrollable";

    if (scroll_obj) {
        char bottom_path[512];
        snprintf(bottom_filename, sizeof(bottom_filename),
                 "%02zu_%s_bottom.bmp", index, id ? id : "missing");
        snprintf(bottom_path, sizeof(bottom_path), "%s/%s", dir,
                 bottom_filename);

        int32_t target_y = lv_obj_get_scroll_y(scroll_obj) + bottom_before;
        lv_obj_scroll_to_y(scroll_obj, target_y, LV_ANIM_OFF);
        run_lvgl_frames(3);
        bottom_after = lv_obj_get_scroll_bottom(scroll_obj);

        if (write_screen_bmp(bottom_path) != 0) {
            (*failures)++;
            scroll_status = "failed";
        } else {
            printf("screenshot: %s\n", bottom_path);
            scroll_status = bottom_after < bottom_before ? "ok" : "failed";
            if (strcmp(scroll_status, "ok") != 0)
                (*failures)++;
        }
    }

    fprintf(scroll_file, "%zu\t%s\t%s\t%d\t%d\t%s\t%s\n", index,
            id ? id : "", scroll_obj ? "yes" : "no", (int)bottom_before,
            (int)bottom_after, bottom_filename, scroll_status);

    fprintf(manifest, "%zu\t%s\t%s\t%s\t%d\t%d\t%s\t%zu\n", index,
            id ? id : "", title ? title : "", filename,
            lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL),
            capture_status, missing_glyphs);
}

static void simulator_capture_load_punch_grid_children(
    const char *dir, FILE *manifest, FILE *glyph_file, FILE *smoke_file,
    FILE *scroll_file, size_t *next_index, int *failures) {
    if (!krux_shell_show_screen("load_punch_grid"))
        return;

    run_lvgl_frames(3);
    simulator_capture_current_page(dir, manifest, glyph_file, smoke_file,
                                   scroll_file, (*next_index)++,
                                   "load_punch_grid_menu", "点阵和1248导入",
                                   failures);

    if (krux_shell_show_screen("load_punch_grid")) {
        run_lvgl_frames(3);
        if (click_button_with_label("点阵板恢复")) {
            simulator_capture_current_page(dir, manifest, glyph_file, smoke_file,
                                           scroll_file, (*next_index)++,
                                           "load_tinyseed_restore",
                                           "点阵板恢复", failures);
            (void)krux_shell_show_screen("load_punch_grid");
            run_lvgl_frames(3);
        } else {
            (*failures)++;
        }
    }

    if (krux_shell_show_screen("load_punch_grid")) {
        run_lvgl_frames(3);
        if (click_button_with_label("1248恢复")) {
            simulator_capture_current_page(dir, manifest, glyph_file, smoke_file,
                                           scroll_file, (*next_index)++,
                                           "load_stackbit_restore", "1248恢复",
                                           failures);
            (void)krux_shell_show_screen("load_punch_grid");
            run_lvgl_frames(3);
        } else {
            (*failures)++;
        }
    }

    (void)krux_shell_show_screen("home");
    run_lvgl_frames(3);
}

static void simulator_capture_backup_export_children(
    const char *dir, FILE *manifest, FILE *glyph_file, FILE *smoke_file,
    FILE *scroll_file, size_t *next_index, int *failures) {
    if (!krux_shell_show_screen("backup_export"))
        return;

    run_lvgl_frames(3);
    if (click_button_with_label("加密")) {
        simulator_capture_current_page(dir, manifest, glyph_file, smoke_file,
                                       scroll_file, (*next_index)++,
                                       "backup_kef", "加密备份文件", failures);
        (void)krux_shell_show_screen("backup_export");
        run_lvgl_frames(3);
    } else {
        (*failures)++;
    }

    (void)krux_shell_show_screen("home");
    run_lvgl_frames(3);
}

static int capture_krux_shell_screens(const char *dir) {
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
        perror(dir);
        return 1;
    }

    char manifest_path[512];
    char glyph_path[512];
    char smoke_path[512];
    char scroll_path[512];
    char interaction_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.tsv", dir);
    snprintf(glyph_path, sizeof(glyph_path), "%s/glyph_check.tsv", dir);
    snprintf(smoke_path, sizeof(smoke_path), "%s/smoke_check.tsv", dir);
    snprintf(scroll_path, sizeof(scroll_path), "%s/scroll_check.tsv", dir);
    snprintf(interaction_path, sizeof(interaction_path), "%s/interaction_check.tsv", dir);

    FILE *manifest = fopen(manifest_path, "w");
    if (!manifest) {
        perror(manifest_path);
        return 1;
    }
    FILE *glyph_file = fopen(glyph_path, "w");
    if (!glyph_file) {
        perror(glyph_path);
        fclose(manifest);
        return 1;
    }
    FILE *smoke_file = fopen(smoke_path, "w");
    if (!smoke_file) {
        perror(smoke_path);
        fclose(glyph_file);
        fclose(manifest);
        return 1;
    }
    FILE *scroll_file = fopen(scroll_path, "w");
    if (!scroll_file) {
        perror(scroll_path);
        fclose(smoke_file);
        fclose(glyph_file);
        fclose(manifest);
        return 1;
    }
    FILE *interaction_file = fopen(interaction_path, "w");
    if (!interaction_file) {
        perror(interaction_path);
        fclose(scroll_file);
        fclose(smoke_file);
        fclose(glyph_file);
        fclose(manifest);
        return 1;
    }

    fprintf(manifest,
            "index\tid\ttitle\tfilename\twidth\theight\tcapture_status\tmissing_glyphs\n");
    fprintf(glyph_file, "screen_id\tcodepoint\tlabel_text\n");
    fprintf(smoke_file, "index\tid\tlabels\tclickables\tstatus\n");
    fprintf(scroll_file,
            "index\tid\tscrollable\tbottom_before\tbottom_after\tbottom_filename\tstatus\n");
    fprintf(interaction_file,
            "start_id\tbutton_label\texpected_id\texpected_text\tactual_id\tstatus\n");

    int failures = 0;
    for (size_t i = 0; i < krux_shell_screen_count(); i++) {
        const char *id = krux_shell_screen_id_at(i);
        const char *title = krux_shell_screen_title_at(i);
        if (id && (strcmp(id, "load_tinyseed_restore") == 0 ||
                   strcmp(id, "load_stackbit_restore") == 0)) {
            continue;
        }
        char filename[256];
        char path[512];
        snprintf(filename, sizeof(filename), "%02zu_%s.bmp", i + 1,
                 id ? id : "missing");
        snprintf(path, sizeof(path), "%s/%s", dir, filename);

        if (!krux_shell_show_screen(id)) {
            fprintf(stderr, "missing screen: %s\n", id ? id : "(null)");
            fprintf(manifest, "%zu\t%s\t%s\t%s\t%d\t%d\tmissing\t0\n",
                    i + 1, id ? id : "", title ? title : "", filename,
                    lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
            failures++;
            continue;
        }

        run_lvgl_frames(3);

        size_t label_count = 0;
        size_t clickable_count = 0;
        count_ui_objects_recursive(lv_screen_active(), &label_count,
                                   &clickable_count);
        const char *smoke_status =
            (label_count > 0 && clickable_count > 0) ? "ok" : "failed";
        if (strcmp(smoke_status, "ok") != 0)
            failures++;
        fprintf(smoke_file, "%zu\t%s\t%zu\t%zu\t%s\n", i + 1,
                id ? id : "", label_count, clickable_count, smoke_status);

        size_t missing_glyphs =
            check_label_glyphs_recursive(lv_screen_active(), glyph_file, id);
        if (missing_glyphs > 0)
            failures++;

        const char *capture_status = "ok";
        if (write_screen_bmp(path) != 0) {
            failures++;
            capture_status = "failed";
        } else {
            printf("screenshot: %s\n", path);
        }

        lv_obj_update_layout(lv_screen_active());
        lv_obj_t *scroll_obj = find_scrollable_object_recursive(lv_screen_active());
        int32_t bottom_before = scroll_obj ? lv_obj_get_scroll_bottom(scroll_obj) : 0;
        int32_t bottom_after = bottom_before;
        char bottom_filename[256] = "";
        const char *scroll_status = scroll_obj ? "ok" : "not_scrollable";

        if (scroll_obj) {
            char bottom_path[512];
            snprintf(bottom_filename, sizeof(bottom_filename),
                     "%02zu_%s_bottom.bmp", i + 1, id ? id : "missing");
            snprintf(bottom_path, sizeof(bottom_path), "%s/%s", dir,
                     bottom_filename);

            int32_t target_y = lv_obj_get_scroll_y(scroll_obj) + bottom_before;
            lv_obj_scroll_to_y(scroll_obj, target_y, LV_ANIM_OFF);
            run_lvgl_frames(3);
            bottom_after = lv_obj_get_scroll_bottom(scroll_obj);

            if (write_screen_bmp(bottom_path) != 0) {
                failures++;
                scroll_status = "failed";
            } else {
                printf("screenshot: %s\n", bottom_path);
                scroll_status = bottom_after < bottom_before ? "ok" : "failed";
                if (strcmp(scroll_status, "ok") != 0)
                    failures++;
            }
        }

        fprintf(scroll_file, "%zu\t%s\t%s\t%d\t%d\t%s\t%s\n", i + 1,
                id ? id : "", scroll_obj ? "yes" : "no",
                (int)bottom_before, (int)bottom_after, bottom_filename,
                scroll_status);

        fprintf(manifest, "%zu\t%s\t%s\t%s\t%d\t%d\t%s\t%zu\n",
                i + 1, id ? id : "", title ? title : "", filename,
                lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL),
                capture_status, missing_glyphs);
    }

    size_t extra_index = krux_shell_screen_count() + 1;
    simulator_capture_load_punch_grid_children(
        dir, manifest, glyph_file, smoke_file, scroll_file, &extra_index,
        &failures);
    simulator_capture_backup_export_children(
        dir, manifest, glyph_file, smoke_file, scroll_file, &extra_index,
        &failures);

    int interaction_failures = run_button_interaction_checks(interaction_file);
    if (interaction_failures > 0)
        failures += interaction_failures;

    fclose(interaction_file);
    fclose(scroll_file);
    fclose(smoke_file);
    fclose(glyph_file);
    fclose(manifest);
    return failures == 0 ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Main                                                                        */
/* -------------------------------------------------------------------------- */

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Kern Desktop Simulator\n\n");
    printf("Options:\n");
    printf("  -q, --qr-image <path>   Load QR image for camera simulation\n");
    printf("  -Q, --qr-dir <path>     Load QR images from directory\n");
    printf("  -d, --data-dir <path>   Data directory (default: simulator/sim_data/)\n");
    printf("  -W, --width <N>         Display width in pixels (default: %d)\n", SIM_LCD_H_RES);
    printf("  -H, --height <N>        Display height in pixels (default: %d)\n", SIM_LCD_V_RES);
    printf("  -S, --screenshot-dir <path>  Capture every Krux shell screen to BMP\n");
    printf("  -w, --webcam [device]   Use webcam (default: /dev/video0)\n");
    printf("  -v, --verbose           Enable DEBUG-level logging\n");
    printf("  -h, --help              Show this help\n");
}

int main(int argc, char *argv[]) {
    /* Restrict permissions on every file the simulator creates (NVS,
     * SD card files, etc.) so they cannot be read by other users. */
    umask(077);

    /* Best-effort: keep pages out of swap and core dumps so a real seed
     * accidentally typed into the simulator does not hit disk.  Both calls
     * are non-fatal — typical desktops cap RLIMIT_MEMLOCK low. */
    (void)mlockall(MCL_CURRENT | MCL_FUTURE);
#if defined(__linux__)
    (void)prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#endif

    fprintf(stderr,
        "\n"
        "  \x1b[1;31m================================================================\x1b[0m\n"
        "  \x1b[1;31m  Kern SIMULATOR - developer build, DO NOT USE WITH REAL FUNDS\x1b[0m\n"
        "  \x1b[1;31m================================================================\x1b[0m\n"
        "\n");

    /* Parse CLI arguments before any init */
    static const struct option long_opts[] = {
        { "qr-image", required_argument, NULL, 'q' },
        { "qr-dir",   required_argument, NULL, 'Q' },
        { "data-dir", required_argument, NULL, 'd' },
        { "width",    required_argument, NULL, 'W' },
        { "height",   required_argument, NULL, 'H' },
        { "screenshot-dir", required_argument, NULL, 'S' },
        { "webcam",   optional_argument, NULL, 'w' },
        { "verbose",  no_argument,       NULL, 'v' },
        { "help",     no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };
    int sim_width = SIM_LCD_H_RES;
    int sim_height = SIM_LCD_V_RES;
    const char *screenshot_dir = NULL;
    int opt;
    while ((opt = getopt_long(argc, argv, "q:Q:d:W:H:S:w::vh", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'q':
                sim_video_set_qr_image(optarg);
                break;
            case 'Q':
                sim_video_set_qr_dir(optarg);
                break;
            case 'd': {
                char nvs_path[512];
                snprintf(nvs_path, sizeof(nvs_path), "%s/nvs", optarg);
                sim_nvs_set_data_dir(nvs_path);
                sim_sdcard_set_data_dir(optarg);
                break;
            }
            case 'W':
                sim_width = atoi(optarg);
                break;
            case 'H':
                sim_height = atoi(optarg);
                break;
            case 'S':
                screenshot_dir = optarg;
                break;
            case 'w':
                sim_video_set_webcam(optarg);
                break;
            case 'v':
                esp_log_level_set("*", ESP_LOG_DEBUG);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                fprintf(stderr,
                    "Usage: %s [--qr-image PATH] [--qr-dir DIR] [--data-dir DIR]"
                    " [--width N] [--height N] [--verbose]\n",
                    argv[0]);
                return 1;
        }
    }

    printf("Kern Simulator starting (%dx%d)\n", sim_width, sim_height);

    /* Initialize LVGL */
    lv_init();

    /* Create SDL2 display */
    lv_display_t *disp = lv_sdl_window_create(sim_width, sim_height);
    if (!disp) {
        fprintf(stderr, "Failed to create SDL display\n");
        return 1;
    }

    lv_sdl_window_set_title(disp, "Kern Simulator");

    /* Create SDL2 mouse input */
    lv_indev_t *mouse = lv_sdl_mouse_create();
    (void)mouse;

    /* Initialize theme (copies Montserrat fonts, sets icon fallbacks) */
    theme_init();

    /* Apply dark background and default text style to screen */
    lv_obj_t *scr = lv_screen_active();
    theme_apply_screen(scr);

    /* Force initial render */
    lv_refr_now(NULL);

    /* -----------------------------------------------------------------------
     * Initialize NVS (file-backed storage for settings and PIN)
     * --------------------------------------------------------------------- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        fprintf(stderr, "NVS init failed: 0x%x\n", ret);
        return 1;
    }

    /* Initialize persistent settings */
    settings_init();

    /* Initialize PMIC (simulated battery on wave_35; no-op on wave_4b) */
    bsp_pmic_init();

    /* Kern is the desktop BSP shim; startup mirrors hardware and enters the
     * Krux migration shell directly instead of the old wallet/login flow. */
    krux_shell_create(scr);
    if (screenshot_dir)
        return capture_krux_shell_screens(screenshot_dir);

    /* -----------------------------------------------------------------------
     * Main loop
     * SDL_QUIT is handled by LVGL's SDL driver which calls exit(0)
     * --------------------------------------------------------------------- */
    while (1) {
        lvgl_port_lock(0);
        uint32_t ms_til_next = lv_timer_handler();
        lvgl_port_unlock();
        /* Cap delay to ~33ms (~30fps).  Background threads (camera stream)
         * update LVGL image sources via lv_img_set_src() which marks the
         * widget dirty, but SDL2 can only render from the main thread.
         * A short cap ensures lv_timer_handler() redraws promptly. */
        if (ms_til_next > 33) ms_til_next = 33;
        SDL_Delay(ms_til_next < 1 ? 1 : ms_til_next);
    }

    return 0;
}
