#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

#include "k_quirc.h"
#include "zbar_qr.h"

#define MAX_PATH_LEN 512
typedef struct {
  const char *name;
  bool use_zbar;
  bool apply_contrast;
  bool find_inverted;
} strategy_t;

typedef struct {
  int total;
  int matched;
  double total_ms;
} strategy_stats_t;

static const strategy_t k_strategies[] = {
    {.name = "zbar_raw", .use_zbar = true},
    {.name = "zbar_contrast", .use_zbar = true, .apply_contrast = true},
    {.name = "quirc_raw"},
    {.name = "quirc_inverted", .find_inverted = true},
    {.name = "quirc_contrast", .apply_contrast = true},
    {.name = "quirc_contrast_inv", .apply_contrast = true, .find_inverted = true},
};

static int ends_with(const char *s, const char *suffix) {
  size_t slen = strlen(s);
  size_t suflen = strlen(suffix);
  if (slen < suflen)
    return 0;
  return strcmp(s + slen - suflen, suffix) == 0;
}

static int cmp_strings(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static double elapsed_ms(const struct timespec *start,
                         const struct timespec *end) {
  double sec = (double)(end->tv_sec - start->tv_sec);
  double nsec = (double)(end->tv_nsec - start->tv_nsec);
  return sec * 1000.0 + nsec / 1e6;
}

static uint8_t *load_png(const char *path, int *w, int *h) {
  int channels = 0;
  return stbi_load(path, w, h, &channels, 1);
}

static char *load_text_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long len = ftell(f);
  if (len < 0) {
    fclose(f);
    return NULL;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }

  char *buf = malloc((size_t)len + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }

  size_t got = fread(buf, 1, (size_t)len, f);
  fclose(f);
  buf[got] = 0;

  while (got > 0) {
    char c = buf[got - 1];
    if (c != '\n' && c != '\r' && c != '\t' && c != ' ')
      break;
    buf[--got] = 0;
  }
  return buf;
}

static bool normalize_contrast(uint8_t *gray, size_t total) {
  if (!gray || total == 0)
    return false;

  uint32_t hist[256] = {0};
  for (size_t i = 0; i < total; i++)
    hist[gray[i]]++;

  size_t low_target = total / 100;
  size_t high_target = total - low_target;
  size_t cumulative = 0;
  uint8_t low = 0;
  uint8_t high = 255;

  for (int i = 0; i < 256; i++) {
    cumulative += hist[i];
    if (cumulative >= low_target) {
      low = (uint8_t)i;
      break;
    }
  }

  cumulative = 0;
  for (int i = 0; i < 256; i++) {
    cumulative += hist[i];
    if (cumulative >= high_target) {
      high = (uint8_t)i;
      break;
    }
  }

  if (high <= low + 24)
    return false;

  uint8_t lut[256];
  uint32_t range = (uint32_t)high - (uint32_t)low;
  for (int i = 0; i < 256; i++) {
    if (i <= low) {
      lut[i] = 0;
    } else if (i >= high) {
      lut[i] = 255;
    } else {
      lut[i] = (uint8_t)(((uint32_t)(i - low) * 255 + range / 2) / range);
    }
  }

  for (size_t i = 0; i < total; i++)
    gray[i] = lut[gray[i]];
  return true;
}

static bool decode_with_zbar(zbar_qr_decoder_t *decoder, const uint8_t *gray,
                             int w, int h, k_quirc_result_t *out) {
  return zbar_qr_decode_grayscale(decoder, gray, w, h, out);
}

static bool decode_with_quirc(k_quirc_t *decoder, const uint8_t *gray, int w,
                              int h, bool find_inverted,
                              k_quirc_result_t *out) {
  if (!decoder || !gray || !out || w <= 0 || h <= 0)
    return false;
  if (k_quirc_resize(decoder, w, h) < 0)
    return false;

  uint8_t *buf = k_quirc_begin(decoder, NULL, NULL);
  if (!buf)
    return false;
  memcpy(buf, gray, (size_t)w * h);
  k_quirc_end(decoder, find_inverted);

  int count = k_quirc_count(decoder);
  for (int i = 0; i < count; i++) {
    k_quirc_result_t result = {0};
    if (k_quirc_decode(decoder, i, &result) == K_QUIRC_SUCCESS &&
        result.valid) {
      *out = result;
      return true;
    }
  }
  return false;
}

static bool run_strategy(const strategy_t *strategy, zbar_qr_decoder_t *zbar,
                         k_quirc_t *quirc, const uint8_t *pixels, int w, int h,
                         char *payload, size_t payload_cap, double *ms_out) {
  if (!strategy || !pixels || !payload || payload_cap == 0 || !ms_out)
    return false;

  size_t total = (size_t)w * (size_t)h;
  uint8_t *work = NULL;
  const uint8_t *input = pixels;
  if (strategy->apply_contrast) {
    work = malloc(total);
    if (!work)
      return false;
    memcpy(work, pixels, total);
    (void)normalize_contrast(work, total);
    input = work;
  }

  struct timespec t0;
  struct timespec t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  bool ok = false;
  k_quirc_result_t result = {0};
  if (strategy->use_zbar) {
    ok = decode_with_zbar(zbar, input, w, h, &result);
  } else {
    ok = decode_with_quirc(quirc, input, w, h, strategy->find_inverted,
                           &result);
  }

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *ms_out = elapsed_ms(&t0, &t1);
  if (ok) {
    int len = result.data.payload_len;
    if (len >= (int)payload_cap)
      len = (int)payload_cap - 1;
    memcpy(payload, result.data.payload, (size_t)len);
    payload[len] = 0;
  } else {
    payload[0] = 0;
  }

  free(work);
  return ok;
}

static int collect_inputs(char ***paths_out, int *count_out, int argc,
                          char **argv) {
  int count = 0;
  char **paths = NULL;

  for (int i = 1; i < argc; i++) {
    const char *path = argv[i];
    struct stat st;
    if (stat(path, &st) != 0)
      continue;

    if (S_ISREG(st.st_mode)) {
      if (!ends_with(path, ".png"))
        continue;
      char **new_paths = realloc(paths, (size_t)(count + 1) * sizeof(*paths));
      if (!new_paths)
        goto fail;
      paths = new_paths;
      paths[count] = strdup(path);
      if (!paths[count])
        goto fail;
      count++;
      continue;
    }

    if (!S_ISDIR(st.st_mode))
      continue;

    DIR *dir = opendir(path);
    if (!dir)
      goto fail;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
      if (!ends_with(ent->d_name, ".png"))
        continue;
      char full[MAX_PATH_LEN];
      snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
      char **new_paths = realloc(paths, (size_t)(count + 1) * sizeof(*paths));
      if (!new_paths) {
        closedir(dir);
        goto fail;
      }
      paths = new_paths;
      paths[count] = strdup(full);
      if (!paths[count]) {
        closedir(dir);
        goto fail;
      }
      count++;
    }
    closedir(dir);
  }

  if (count == 0)
    goto fail;

  qsort(paths, (size_t)count, sizeof(*paths), cmp_strings);
  *paths_out = paths;
  *count_out = count;
  return 0;

fail:
  for (int i = 0; i < count; i++)
    free(paths[i]);
  free(paths);
  return -1;
}

static void make_text_path(const char *png_path, char *out, size_t out_len) {
  snprintf(out, out_len, "%s", png_path);
  char *dot = strrchr(out, '.');
  if (dot)
    strcpy(dot, ".txt");
  else
    strncat(out, ".txt", out_len - strlen(out) - 1);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <png-file-or-dir> [more...]\n", argv[0]);
    return 2;
  }

  char **paths = NULL;
  int path_count = 0;
  if (collect_inputs(&paths, &path_count, argc, argv) != 0) {
    fprintf(stderr, "No PNG inputs found\n");
    return 1;
  }

  zbar_qr_decoder_t *zbar = zbar_qr_decoder_create();
  k_quirc_t *quirc = k_quirc_new();
  if (!zbar || !quirc) {
    fprintf(stderr, "Failed to create decoders\n");
    zbar_qr_decoder_destroy(zbar);
    k_quirc_destroy(quirc);
    for (int i = 0; i < path_count; i++)
      free(paths[i]);
    free(paths);
    return 1;
  }

  strategy_stats_t stats[sizeof(k_strategies) / sizeof(k_strategies[0])] = {0};

  printf("%-32s %-7s", "Sample", "Expect");
  for (size_t i = 0; i < sizeof(k_strategies) / sizeof(k_strategies[0]); i++)
    printf("  %-18s", k_strategies[i].name);
  printf("\n");

  for (int i = 0; i < path_count; i++) {
    const char *png_path = paths[i];
    char txt_path[MAX_PATH_LEN];
    make_text_path(png_path, txt_path, sizeof(txt_path));

    int w = 0;
    int h = 0;
    uint8_t *pixels = load_png(png_path, &w, &h);
    if (!pixels) {
      printf("%-32s %-7s  load failed\n", png_path, "ERR");
      continue;
    }

    char *expected = load_text_file(txt_path);
    const char *expect_tag = expected ? "yes" : "raw";
    if (k_quirc_resize(quirc, w, h) < 0) {
      printf("%-32s %-7s  resize failed\n", png_path, "ERR");
      stbi_image_free(pixels);
      free(expected);
      continue;
    }

    printf("%-32s %-7s", png_path, expect_tag);

    for (size_t s = 0; s < sizeof(k_strategies) / sizeof(k_strategies[0]); s++) {
      char payload[9000];
      double ms = 0.0;
      bool ok = run_strategy(&k_strategies[s], zbar, quirc, pixels, w, h,
                             payload, sizeof(payload), &ms);
      bool match = false;
      if (ok) {
        if (expected) {
          match = strcmp(payload, expected) == 0;
        } else {
          match = true;
        }
      }

      stats[s].total++;
      stats[s].total_ms += ms;
      if (match)
        stats[s].matched++;

      char preview[32];
      if (!ok) {
        snprintf(preview, sizeof(preview), "no");
      } else if (match) {
        int len = (int)strlen(payload);
        snprintf(preview, sizeof(preview), "ok len=%d %.1f", len, ms);
      } else {
        snprintf(preview, sizeof(preview), "bad %.1f", ms);
      }

      printf("  %-18s", preview);
    }

    printf("\n");

    if (expected) {
      printf("  expected len=%zu\n", strlen(expected));
    }

    stbi_image_free(pixels);
    free(expected);
  }

  printf("\nSummary\n");
  for (size_t s = 0; s < sizeof(k_strategies) / sizeof(k_strategies[0]); s++) {
    double avg = stats[s].total ? stats[s].total_ms / (double)stats[s].total : 0.0;
    printf("%-18s %d/%d avg=%.2fms\n", k_strategies[s].name, stats[s].matched,
           stats[s].total, avg);
  }

  zbar_qr_decoder_destroy(zbar);
  k_quirc_destroy(quirc);
  for (int i = 0; i < path_count; i++)
    free(paths[i]);
  free(paths);
  return 0;
}
