#include <dirent.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include "vendor/miniz/miniz.h"

#ifndef PS5_GAME_DIR
#define PS5_GAME_DIR "/data/homebrew"
#endif
#ifndef PS5_STAGE_DIR
#define PS5_STAGE_DIR "/data/romm-ps5/staging"
#endif

static int
ps5_suffix(const char *name, const char *suffix) {
  size_t a = strlen(name), b = strlen(suffix);
  return a >= b && !strcasecmp(name + a - b, suffix);
}

static const char *
ps5_image_ext(const char *name) {
  static const char *exts[] = {".ffpkg", ".exfat", ".ffpfs", ".ffpfsc"};
  for (size_t i = 0; i < sizeof exts / sizeof exts[0]; i++)
    if (ps5_suffix(name, exts[i])) return exts[i];
  return NULL;
}

/* Never follow symlinks when creating parents or deleting our staging tree. */
static int
ps5_mkdirs(const char *path) {
  char buf[1200];
  struct stat st;
  if (strlen(path) >= sizeof buf) return -1;
  strcpy(buf, path);
  for (char *p = buf + 1; ; p++) {
    if (*p != '/' && *p) continue;
    char saved = *p; *p = 0;
    if (mkdir(buf, 0755) && errno != EEXIST) return -1;
    if (lstat(buf, &st) || !S_ISDIR(st.st_mode)) return -1;
    *p = saved;
    if (!saved) break;
  }
  return 0;
}

static void
ps5_remove_stage(const char *path) {
  struct stat st;
  if (lstat(path, &st)) return;
  if (!S_ISDIR(st.st_mode)) { unlink(path); return; }
  DIR *dir = opendir(path);
  if (!dir) return;
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    char child[1200];
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
    int n = snprintf(child, sizeof child, "%s/%s", path, entry->d_name);
    if (n > 0 && (size_t)n < sizeof child) ps5_remove_stage(child);
  }
  closedir(dir);
  rmdir(path);
}

static int
ps5_zip_name(mz_zip_archive *zip, mz_uint index, char name[768]) {
  mz_uint size = mz_zip_reader_get_filename(zip, index, name, 768);
  if (!size || size > 768 || strlen(name) != size - 1 || name[0] == '/') return -1;
  unsigned depth = 0;
  const char *start = name;
  for (const char *p = name; ; p++) {
    if (*p == '\\' || *p == ':' || (*p && (unsigned char)*p < 32)) return -1;
    if (*p == '/' || !*p) {
      size_t len = (size_t)(p - start);
      if ((!len && *p) || (len == 1 && *start == '.') ||
          (len == 2 && start[0] == '.' && start[1] == '.') || len > 255 || ++depth > 32) return -1;
      if (!*p) break;
      start = p + 1;
    }
  }
  return name[0] ? 0 : -1;
}

typedef struct {
  FILE *fp;
  uint64_t file_written, total_written, expected;
  time_t last_log;
} ps5_extract_sink;

static size_t
ps5_zip_write(void *opaque, mz_uint64 offset, const void *data, size_t size) {
  ps5_extract_sink *sink = opaque;
  if (offset != sink->file_written || fwrite(data, 1, size, sink->fp) != size) return 0;
  sink->file_written += size;
  sink->total_written += size;
  transfer_progress(sink->total_written, sink->expected);
  if (time(NULL) - sink->last_log >= 5) {
    printf("[ps5] extracted=%llu/%llu bytes\n", (unsigned long long)sink->total_written,
           (unsigned long long)sink->expected);
    sink->last_log = time(NULL);
  }
  return size;
}

/* A completed source is only exposed to the mounter after CRC verification.
 * Flatten any archive wrapper by locating exactly one sce_sys/param.json
 * and its sibling eboot.bin, or exactly one supported image file.
 */
static int
ps5_prepare(const char *source, const char *name, long rom_id, char *result, size_t result_size) {
  mz_zip_archive zip = {0};
  char stage[1200] = "", final[1200], root[768] = "", image_name[768] = "";
  struct stat st;
  uint64_t expanded = 0;
  mz_uint count = 0, roots = 0, images = 0;
  int opened = 0, ok = 0;
  const char *ext = ps5_image_ext(name);
  snprintf(result, result_size, "Could not prepare PS5 files. Source retained; see terminal.");
  if (rom_id < 0 || lstat(source, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0) goto done;
  if (ps5_mkdirs(PS5_GAME_DIR) || ps5_mkdirs(PS5_STAGE_DIR)) goto done;
  if (ext) {
    snprintf(final, sizeof final, "%s/romm-%ld%s", PS5_GAME_DIR, rom_id, ext);
    /* Same-filesystem hard link is atomic, retains the download, and refuses
       to overwrite anything. The image content is validated by the mounter. */
    if (link(source, final)) {
      snprintf(result, result_size, "PS5 destination exists or cannot be created. Source retained.");
      goto done;
    }
    ok = 1;
    goto published;
  }
  if (!ps5_suffix(name, ".zip") || !mz_zip_reader_init_file(&zip, source, 0)) {
    snprintf(result, result_size, "Cannot open ZIP. Use a single unencrypted ZIP/ZIP64 archive. Source retained.");
    goto done;
  }
  opened = 1;
  count = mz_zip_reader_get_num_files(&zip);
  if (!count || count > 100000) goto invalid;
  for (mz_uint i = 0; i < count; i++) {
    char entry[768];
    mz_zip_archive_file_stat info;
    if (ps5_zip_name(&zip, i, entry) || !mz_zip_reader_file_stat(&zip, i, &info)) goto invalid;
    unsigned type = (info.m_external_attr >> 16) & 0170000;
    if ((type && type != 0100000 && type != 0040000) || info.m_is_encrypted ||
        !info.m_is_supported || (info.m_method != 0 && info.m_method != 8)) goto invalid;
    if (info.m_is_directory) continue;
    if (UINT64_MAX - expanded < info.m_uncomp_size) goto invalid;
    expanded += info.m_uncomp_size;
    const char *marker = "sce_sys/param.json";
    size_t len = strlen(entry), mlen = strlen(marker);
    if (len >= mlen && !strcmp(entry + len - mlen, marker) &&
        (len == mlen || entry[len - mlen - 1] == '/')) {
      roots++;
      memcpy(root, entry, len - mlen); root[len - mlen] = 0;
    }
    if (ps5_image_ext(entry)) { images++; strcpy(image_name, entry); }
  }
  if (roots == 1) {
    char eboot[800];
    snprintf(eboot, sizeof eboot, "%seboot.bin", root);
    int index = mz_zip_reader_locate_file(&zip, eboot, NULL, MZ_ZIP_FLAG_CASE_SENSITIVE);
    mz_zip_archive_file_stat info;
    if (index < 0 || !mz_zip_reader_file_stat(&zip, (mz_uint)index, &info) || info.m_is_directory || !info.m_uncomp_size) goto invalid;
    snprintf(final, sizeof final, "%s/romm-%ld", PS5_GAME_DIR, rom_id);
  } else if (!roots && images == 1) {
    snprintf(final, sizeof final, "%s/romm-%ld%s", PS5_GAME_DIR, rom_id, ps5_image_ext(image_name));
  } else goto invalid;
  if (!lstat(final, &st) || errno != ENOENT) {
    snprintf(result, result_size, "PS5 destination already exists. It was not replaced. Source retained.");
    goto done;
  }
  struct statvfs space;
  if (statvfs(PS5_STAGE_DIR, &space) || !space.f_frsize ||
      space.f_bavail < count ||
      expanded / space.f_frsize + (expanded % space.f_frsize != 0) > space.f_bavail - count) {
    snprintf(result, result_size, "Not enough free space for extracted files, or space check failed. ZIP retained.");
    goto done;
  }
  printf("[ps5] ZIP%s entries=%u expanded=%llu destination=%s\n", mz_zip_is_zip64(&zip) ? "64" : "",
         count, (unsigned long long)expanded, final);
  snprintf(stage, sizeof stage, "%s/romm-%ld-XXXXXX", PS5_STAGE_DIR, rom_id);
  if (!mkdtemp(stage)) { stage[0] = 0; goto done; }
  transfer_message("Extracting PS5 archive...");
  ps5_extract_sink sink = {.expected = expanded};
  for (mz_uint i = 0; i < count; i++) {
    char entry[768], dest[1200], parent[1200];
    mz_zip_archive_file_stat info;
    if (ps5_zip_name(&zip, i, entry) || !mz_zip_reader_file_stat(&zip, i, &info)) goto done;
    int n = snprintf(dest, sizeof dest, "%s/%s", stage, entry);
    if (n < 0 || (size_t)n >= sizeof dest) goto done;
    if (info.m_is_directory) {
      size_t len = strlen(dest);
      if (dest[len - 1] == '/') dest[len - 1] = 0;
      if (ps5_mkdirs(dest)) goto done;
      continue;
    }
    strcpy(parent, dest); *strrchr(parent, '/') = 0;
    if (ps5_mkdirs(parent)) goto done;
    int fd = open(dest, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) goto done;
    sink.fp = fdopen(fd, "wb");
    if (!sink.fp) { close(fd); goto done; }
    sink.file_written = 0;
    int extracted = mz_zip_reader_extract_to_callback(&zip, i, ps5_zip_write, &sink, 0);
    int flushed = fclose(sink.fp);
    sink.fp = NULL;
    if (!extracted || flushed || sink.file_written != info.m_uncomp_size) {
      printf("[ps5] extraction failed entry=%s error=%s\n", entry, mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
      goto done;
    }
  }
  char selected[1200];
  int n = snprintf(selected, sizeof selected, "%s/%s", stage, roots ? root : image_name);
  if (n < 0 || (size_t)n >= sizeof selected) goto done;
  size_t slen = strlen(selected);
  if (selected[slen - 1] == '/') selected[slen - 1] = 0;
  /* Do not replace an existing game, including one created during extraction. */
  if (!lstat(final, &st) || errno != ENOENT || rename(selected, final)) goto done;
  ok = 1;
published:
  printf("[ps5] prepared=%s; ShadowMountPlus scan required\n", final);
  snprintf(result, result_size, "PS5 files ready at %.120s. ShadowMountPlus must scan them; launch compatibility is not verified.", final);
  goto done;
invalid:
  snprintf(result, result_size, "Unsupported ZIP layout/entry. Use one game folder (eboot.bin + sce_sys/param.json) or one PS5 image. ZIP retained.");
done:
  if (opened) mz_zip_reader_end(&zip);
  if (stage[0]) ps5_remove_stage(stage);
  if (!ok) printf("[ps5] %s\n", result);
  return ok ? 0 : -1;
}
