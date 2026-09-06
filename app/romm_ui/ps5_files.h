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

/* Build with -DPS5_EXTRACT_PROFILE=1 to log where extraction time goes.
 * Off by default: zero overhead, no behavior change. Callback timings
 * (fwrite/progress) happen inside the extract-call timing, not beside it,
 * so extract_call minus (fwrite+progress) is archive read+decompress+CRC. */
#ifndef PS5_EXTRACT_PROFILE
#define PS5_EXTRACT_PROFILE 0
#endif

#if PS5_EXTRACT_PROFILE
#include <time.h>

typedef struct {
  double total_s, mkdirs_open_s, extract_call_s, fwrite_s, progress_s, fclose_s;
  uint64_t files, dirs, callbacks;
} ps5_extract_profile;

static double
ps5_prof_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void
ps5_prof_report(const ps5_extract_profile *p) {
  double callback_s = p->fwrite_s + p->progress_s;
  double decompress_s = p->extract_call_s - callback_s;
  printf("[ps5][profile] total=%.3fs mkdirs+open=%.3fs extract_call=%.3fs "
         "(fwrite=%.3fs progress=%.3fs decompress_est=%.3fs) fclose=%.3fs "
         "unaccounted=%.3fs files=%llu dirs=%llu callbacks=%llu\n",
         p->total_s, p->mkdirs_open_s, p->extract_call_s, p->fwrite_s, p->progress_s,
         decompress_s < 0 ? 0.0 : decompress_s, p->fclose_s,
         p->total_s - p->mkdirs_open_s - p->extract_call_s - p->fclose_s,
         (unsigned long long)p->files, (unsigned long long)p->dirs,
         (unsigned long long)p->callbacks);
}
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
    /* mkdir()'s mode is subject to umask; fchmod-style verification via
       chmod() ignores it, but only for directories this call just created,
       never a pre-existing parent it doesn't own. */
    int created = !mkdir(buf, 0777);
    if (!created && errno != EEXIST) return -1;
    if (created && chmod(buf, 0777)) return -1;
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
  uint64_t last_written;
  mz_uint entries_done, entries_total;
#if PS5_EXTRACT_PROFILE
  ps5_extract_profile *prof;
#endif
} ps5_extract_sink;

static size_t
ps5_zip_write(void *opaque, mz_uint64 offset, const void *data, size_t size) {
  ps5_extract_sink *sink = opaque;
#if PS5_EXTRACT_PROFILE
  sink->prof->callbacks++;
  double t0 = ps5_prof_now(), t1;
#endif
  if (offset != sink->file_written) return 0;
  size_t written = fwrite(data, 1, size, sink->fp);
#if PS5_EXTRACT_PROFILE
  t1 = ps5_prof_now();
  sink->prof->fwrite_s += t1 - t0;
#endif
  if (written != size) return 0;
  sink->file_written += size;
  sink->total_written += size;
  transfer_progress(sink->total_written, sink->expected);
  time_t now = time(NULL);
  if (now - sink->last_log >= 5) {
    printf("[ps5] extracted=%llu/%llu bytes entries=%u/%u speed=%.2f MB/s\n",
           (unsigned long long)sink->total_written, (unsigned long long)sink->expected,
           sink->entries_done, sink->entries_total,
           (double)(sink->total_written - sink->last_written) / difftime(now, sink->last_log) / 1000000.0);
    sink->last_written = sink->total_written;
    sink->last_log = now;
  }
#if PS5_EXTRACT_PROFILE
  sink->prof->progress_s += ps5_prof_now() - t1;
#endif
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
  char *write_buffer = NULL;
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
  printf("[ps5] ZIP%s preflight entries=%u\n", mz_zip_is_zip64(&zip) ? "64" : "", count);
  /* miniz validates and allocates the central directory. Real PS5 games can
     exceed 100,000 entries; validate every entry instead of imposing that cap. */
  if (!count) {
    snprintf(result, result_size, "ZIP is empty. ZIP retained.");
    goto done;
  }
  for (mz_uint i = 0; i < count; i++) {
    char entry[768];
    mz_zip_archive_file_stat info;
    if (ps5_zip_name(&zip, i, entry)) {
      snprintf(result, result_size, "ZIP entry %u has an unsafe or overlong path. ZIP retained.", i);
      goto done;
    }
    if (!mz_zip_reader_file_stat(&zip, i, &info)) {
      snprintf(result, result_size, "Cannot read ZIP entry %u metadata. ZIP retained.", i);
      goto done;
    }
    unsigned type = (info.m_external_attr >> 16) & 0170000;
    if ((type && type != 0100000 && type != 0040000) || info.m_is_encrypted ||
        !info.m_is_supported || (info.m_method != 0 && info.m_method != 8)) {
      printf("[ps5] rejected entry=%s type=%o encrypted=%u supported=%u method=%u\n",
             entry, type, info.m_is_encrypted, info.m_is_supported, info.m_method);
      snprintf(result, result_size, "ZIP entry %u has unsupported type/compression or encryption. See terminal. ZIP retained.", i);
      goto done;
    }
    if (info.m_is_directory) continue;
    if (UINT64_MAX - expanded < info.m_uncomp_size) {
      snprintf(result, result_size, "ZIP expanded size overflows at entry %u. ZIP retained.", i);
      goto done;
    }
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
    if (index < 0 || !mz_zip_reader_file_stat(&zip, (mz_uint)index, &info) || info.m_is_directory || !info.m_uncomp_size) {
      printf("[ps5] missing or invalid executable=%s\n", eboot);
      snprintf(result, result_size, "ZIP game root lacks a nonempty eboot.bin. ZIP retained.");
      goto done;
    }
    snprintf(final, sizeof final, "%s/romm-%ld", PS5_GAME_DIR, rom_id);
  } else if (!roots && images == 1) {
    snprintf(final, sizeof final, "%s/romm-%ld%s", PS5_GAME_DIR, rom_id, ps5_image_ext(image_name));
  } else {
    snprintf(result, result_size, "ZIP contains %u game roots and %u PS5 images; expected one game root or one image. ZIP retained.", roots, images);
    goto done;
  }
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
  /* mkdtemp() creates the directory 0700. When the game root is the archive
     root itself (no wrapper folder), this directory is renamed straight into
     PS5_GAME_DIR, so fix its mode or ShadowMountPlus cannot read into it.
     0755 (execute bit only) was confirmed insufficient on-console; the
     working reference copy used 0777 throughout, so match that exactly. */
  if (chmod(stage, 0777)) goto done;
  transfer_message("Extracting PS5 archive...");
  ps5_extract_sink sink = {.expected = expanded, .last_log = time(NULL), .entries_total = count};
  /* The staging directory is private and ZIP symlinks are rejected. Reuse
     the last verified parent for adjacent files instead of walking it again. */
  char last_parent[1200] = "";
  write_buffer = malloc(256 * 1024);
#if PS5_EXTRACT_PROFILE
  ps5_extract_profile prof = {0};
  sink.prof = &prof;
  double prof_total_start = ps5_prof_now(), t;
#endif
  for (mz_uint i = 0; i < count; i++) {
    char entry[768], dest[1200], parent[1200];
    mz_zip_archive_file_stat info;
    if (ps5_zip_name(&zip, i, entry) || !mz_zip_reader_file_stat(&zip, i, &info)) goto done;
    int n = snprintf(dest, sizeof dest, "%s/%s", stage, entry);
    if (n < 0 || (size_t)n >= sizeof dest) goto done;
    sink.entries_done = i;
    if (info.m_is_directory) {
      size_t len = strlen(dest);
      if (dest[len - 1] == '/') dest[len - 1] = 0;
#if PS5_EXTRACT_PROFILE
      t = ps5_prof_now();
#endif
      int failed = ps5_mkdirs(dest);
#if PS5_EXTRACT_PROFILE
      prof.mkdirs_open_s += ps5_prof_now() - t;
      prof.dirs++;
#endif
      if (failed) goto done;
      continue;
    }
    strcpy(parent, dest); *strrchr(parent, '/') = 0;
#if PS5_EXTRACT_PROFILE
    t = ps5_prof_now();
#endif
    if (strcmp(parent, last_parent)) {
      if (ps5_mkdirs(parent)) goto done;
      strcpy(last_parent, parent);
    }
    /* 0644 (no execute bit) left eboot.bin unlaunchable even after the
       directory-mode fix. 0755 was tried next and confirmed insufficient
       on-console too; the working reference copy was 0777 throughout, so
       match that exactly instead of guessing at a stricter mode again.
       fchmod() ignores umask, so the requested mode always lands exactly. */
    int fd = open(dest, O_WRONLY | O_CREAT | O_EXCL, 0777);
    if (fd >= 0 && fchmod(fd, 0777)) { close(fd); fd = -1; }
#if PS5_EXTRACT_PROFILE
    prof.mkdirs_open_s += ps5_prof_now() - t;
    prof.files++;
#endif
    if (fd < 0) goto done;
    sink.fp = fdopen(fd, "wb");
    if (!sink.fp) { close(fd); goto done; }
    if (write_buffer) (void)setvbuf(sink.fp, write_buffer, _IOFBF, 256 * 1024);
    sink.file_written = 0;
#if PS5_EXTRACT_PROFILE
    t = ps5_prof_now();
#endif
    int extracted = mz_zip_reader_extract_to_callback(&zip, i, ps5_zip_write, &sink, 0);
#if PS5_EXTRACT_PROFILE
    prof.extract_call_s += ps5_prof_now() - t;
    t = ps5_prof_now();
#endif
    int flushed = fclose(sink.fp);
#if PS5_EXTRACT_PROFILE
    prof.fclose_s += ps5_prof_now() - t;
#endif
    sink.fp = NULL;
    if (!extracted || flushed || sink.file_written != info.m_uncomp_size) {
      printf("[ps5] extraction failed entry=%s error=%s\n", entry, mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
      goto done;
    }
  }
#if PS5_EXTRACT_PROFILE
  prof.total_s = ps5_prof_now() - prof_total_start;
  ps5_prof_report(&prof);
#endif
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
done:
  free(write_buffer);
  if (opened) mz_zip_reader_end(&zip);
  if (stage[0]) ps5_remove_stage(stage);
  if (!ok) printf("[ps5] %s\n", result);
  return ok ? 0 : -1;
}
