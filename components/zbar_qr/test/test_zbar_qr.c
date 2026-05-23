#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

#include "zbar_qr.h"

#define PAYLOAD_DISPLAY_LEN 48

static int ends_with(const char *s, const char *suffix) {
  size_t slen = strlen(s);
  size_t suflen = strlen(suffix);
  if (slen < suflen)
    return 0;
  return strcmp(s + slen - suflen, suffix) == 0;
}

static int cmp_strings(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

static uint8_t *load_png(const char *path, int *w, int *h) {
  int channels;
  return stbi_load(path, w, h, &channels, 1);
}

static int test_one_file(zbar_qr_decoder_t *decoder, const char *label,
                         const char *path, int *total, int *decoded) {
  int w = 0;
  int h = 0;
  uint8_t *pixels = load_png(path, &w, &h);
  if (!pixels) {
    printf("  %-32s LOADERR\n", label);
    return 0;
  }

  k_quirc_result_t result;
  bool ok = zbar_qr_decode_grayscale(decoder, pixels, w, h, &result);
  (*total)++;
  if (ok) {
    (*decoded)++;
    int preview_len = result.data.payload_len;
    if (preview_len > PAYLOAD_DISPLAY_LEN)
      preview_len = PAYLOAD_DISPLAY_LEN;
    printf("  %-32s YES len=%d %.*s%s\n", label, result.data.payload_len,
           preview_len, result.data.payload,
           result.data.payload_len > PAYLOAD_DISPLAY_LEN ? "..." : "");
  } else {
    printf("  %-32s NO\n", label);
  }

  stbi_image_free(pixels);
  return ok ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <png-file-or-dir> [png-file-or-dir...]\n",
            argv[0]);
    return 2;
  }

  zbar_qr_decoder_t *decoder = zbar_qr_decoder_create();
  if (!decoder) {
    fprintf(stderr, "Failed to create zbar QR decoder\n");
    return 1;
  }

  int total = 0;
  int decoded = 0;

  for (int arg = 1; arg < argc; arg++) {
    const char *dir_path = argv[arg];
    struct stat st;
    if (stat(dir_path, &st) == 0 && S_ISREG(st.st_mode)) {
      printf("%s\n", dir_path);
      (void)test_one_file(decoder, dir_path, dir_path, &total, &decoded);
      continue;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) {
      fprintf(stderr, "Cannot open %s\n", dir_path);
      zbar_qr_decoder_destroy(decoder);
      return 1;
    }

    int capacity = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
      if (ends_with(ent->d_name, ".png"))
        capacity++;
    }
    rewinddir(dir);

    char **names = calloc((size_t)capacity, sizeof(*names));
    if (!names) {
      closedir(dir);
      zbar_qr_decoder_destroy(decoder);
      return 1;
    }

    int count = 0;
    while ((ent = readdir(dir)) != NULL) {
      if (ends_with(ent->d_name, ".png"))
        names[count++] = strdup(ent->d_name);
    }
    closedir(dir);
    qsort(names, (size_t)count, sizeof(*names), cmp_strings);

    printf("%s\n", dir_path);
    for (int i = 0; i < count; i++) {
      char path[512];
      snprintf(path, sizeof(path), "%s/%s", dir_path, names[i]);
      (void)test_one_file(decoder, names[i], path, &total, &decoded);
      free(names[i]);
    }
    free(names);
  }

  zbar_qr_decoder_destroy(decoder);

  printf("Summary: %d/%d decoded\n", decoded, total);
  return decoded == total ? 0 : 1;
}
