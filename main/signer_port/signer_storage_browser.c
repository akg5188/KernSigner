#include "signer_storage_browser.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "sd_card.h"

#define SIGNER_STORAGE_BROWSER_MAX_FILES 12
#define SIGNER_STORAGE_BROWSER_NAME_LEN 256
#define SIGNER_STORAGE_BROWSER_PATH_LEN 512

typedef struct {
  char name[SIGNER_STORAGE_BROWSER_NAME_LEN];
  off_t size;
} signer_storage_browser_file_t;

typedef struct {
  size_t file_count;
  size_t shown_count;
  size_t dir_count;
  size_t skipped_count;
  unsigned long long total_file_bytes;
} signer_storage_browser_stats_t;

typedef struct {
  char *buf;
  size_t len;
  size_t used;
  bool truncated;
} signer_storage_browser_output_t;

static esp_err_t signer_storage_browser_append(
    signer_storage_browser_output_t *out, const char *fmt, ...) {
  if (!out || !out->buf || out->len == 0)
    return ESP_ERR_INVALID_ARG;
  if (out->used >= out->len) {
    out->truncated = true;
    return ESP_ERR_INVALID_SIZE;
  }

  va_list args;
  va_start(args, fmt);
  int written = vsnprintf(out->buf + out->used, out->len - out->used, fmt, args);
  va_end(args);

  if (written < 0) {
    out->truncated = true;
    return ESP_FAIL;
  }

  if ((size_t)written >= out->len - out->used) {
    out->used = out->len - 1;
    out->truncated = true;
    return ESP_ERR_INVALID_SIZE;
  }

  out->used += (size_t)written;
  return ESP_OK;
}

static esp_err_t signer_storage_browser_make_path(char *path, size_t path_len,
                                                const char *name) {
  int written =
      snprintf(path, path_len, "%s/%s", SD_CARD_MOUNT_POINT, name ? name : "");
  if (written < 0 || (size_t)written >= path_len)
    return ESP_ERR_INVALID_SIZE;
  return ESP_OK;
}

static bool signer_storage_browser_safe_name(const char *name) {
  if (!name || !name[0])
    return false;
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    return false;
  if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\'))
    return false;
  return true;
}

static void signer_storage_browser_consider_file(
    signer_storage_browser_file_t files[SIGNER_STORAGE_BROWSER_MAX_FILES],
    signer_storage_browser_stats_t *stats, const char *name, off_t size) {
  if (!files || !stats || !name)
    return;

  size_t pos = 0;
  while (pos < stats->shown_count && strcmp(files[pos].name, name) < 0)
    pos++;

  if (pos >= SIGNER_STORAGE_BROWSER_MAX_FILES)
    return;

  size_t move_count = stats->shown_count > pos ? stats->shown_count - pos : 0;
  if (stats->shown_count == SIGNER_STORAGE_BROWSER_MAX_FILES) {
    if (move_count > 0)
      memmove(&files[pos + 1], &files[pos],
              (move_count - 1) * sizeof(files[0]));
  } else {
    if (move_count > 0)
      memmove(&files[pos + 1], &files[pos], move_count * sizeof(files[0]));
    stats->shown_count++;
  }

  snprintf(files[pos].name, sizeof(files[pos].name), "%s", name);
  files[pos].size = size;
}

static esp_err_t signer_storage_browser_scan_root(
    signer_storage_browser_file_t files[SIGNER_STORAGE_BROWSER_MAX_FILES],
    signer_storage_browser_stats_t *stats) {
  if (!files || !stats)
    return ESP_ERR_INVALID_ARG;

  memset(stats, 0, sizeof(*stats));

  DIR *dir = opendir(SD_CARD_MOUNT_POINT);
  if (!dir)
    return ESP_FAIL;

  struct dirent *entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (!signer_storage_browser_safe_name(entry->d_name)) {
      stats->skipped_count++;
      continue;
    }

    char path[SIGNER_STORAGE_BROWSER_PATH_LEN];
    esp_err_t path_ret =
        signer_storage_browser_make_path(path, sizeof(path), entry->d_name);
    if (path_ret != ESP_OK) {
      stats->skipped_count++;
      continue;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
      stats->skipped_count++;
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      stats->dir_count++;
      continue;
    }

    if (!S_ISREG(st.st_mode)) {
      stats->skipped_count++;
      continue;
    }

    stats->total_file_bytes += (unsigned long long)st.st_size;
    signer_storage_browser_consider_file(files, stats, entry->d_name, st.st_size);
    stats->file_count++;
  }

  closedir(dir);
  return ESP_OK;
}

esp_err_t signer_storage_browser_format_root(char *buf, size_t buf_len) {
  if (!buf || buf_len == 0)
    return ESP_ERR_INVALID_ARG;

  buf[0] = '\0';

  signer_storage_browser_output_t out = {
      .buf = buf,
      .len = buf_len,
      .used = 0,
      .truncated = false,
  };

  esp_err_t ret = signer_storage_browser_append(
      &out, "存储浏览\n挂载点：%s\n", SD_CARD_MOUNT_POINT);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_SIZE)
    return ret;

  ret = sd_card_init();
  if (ret != ESP_OK) {
    signer_storage_browser_append(&out, "错误：无法挂载 SD 卡（%s）。\n",
                                esp_err_to_name(ret));
    return ret;
  }

  signer_storage_browser_file_t files[SIGNER_STORAGE_BROWSER_MAX_FILES] = {0};
  signer_storage_browser_stats_t stats = {0};
  ret = signer_storage_browser_scan_root(files, &stats);
  if (ret != ESP_OK) {
    signer_storage_browser_append(&out, "错误：无法读取 SD 卡根目录。\n");
    return ret;
  }

  signer_storage_browser_append(
      &out,
      "普通文件：%u 个\n目录：%u 个\n总大小：%llu 字节\n已跳过：%u 个不安全或非普通条目\n",
      (unsigned int)stats.file_count, (unsigned int)stats.dir_count,
      stats.total_file_bytes, (unsigned int)stats.skipped_count);

  if (stats.file_count == 0) {
    signer_storage_browser_append(&out, "根目录没有普通文件。\n");
  } else {
    signer_storage_browser_append(&out, "按文件名排序，最多显示前 %d 个文件：\n",
                                SIGNER_STORAGE_BROWSER_MAX_FILES);
    for (size_t i = 0; i < stats.shown_count; i++) {
      signer_storage_browser_append(&out, "%u. %s（%lld 字节）\n",
                                  (unsigned int)(i + 1), files[i].name,
                                  (long long)files[i].size);
    }
    if (stats.file_count > stats.shown_count) {
      signer_storage_browser_append(&out, "还有 %u 个文件未显示。\n",
                                  (unsigned int)(stats.file_count -
                                                 stats.shown_count));
    }
  }

  if (out.truncated)
    return ESP_ERR_INVALID_SIZE;
  return ESP_OK;
}
