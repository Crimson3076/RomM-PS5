/* Read-only structural diagnostics. Field layouts checked against:
 * https://github.com/maxton/LibOrbisPkg/blob/master/LibOrbisPkg/PKG/PkgReader.cs
 * https://github.com/maxton/LibOrbisPkg/blob/master/LibOrbisPkg/PKG/Entry.cs
 * https://github.com/maxton/LibOrbisPkg/blob/master/LibOrbisPkg/PlayGo/ChunkDat.cs
 * These checks do not authenticate signatures or prove retail/FPKG status.
 */
static uint32_t
diag_be32(const unsigned char *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
         (uint32_t)p[2] << 8 | p[3];
}

static uint32_t
diag_le32(const unsigned char *p) {
  return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 |
         (uint32_t)p[1] << 8 | p[0];
}

static unsigned
diag_le16(const unsigned char *p) { return (unsigned)p[1] << 8 | p[0]; }

static int
diag_range(uint64_t offset, uint64_t length, uint64_t size) {
  return offset <= size && length <= size - offset;
}

static int
diag_playgo(const unsigned char *p, size_t size, const char *content_id) {
  static const char *names[8] = {"chunk_attrs", "chunk_mchunks", "chunk_labels",
    "mchunk_attrs", "scenario_attrs", "scenario_chunks", "scenario_labels", "inner_mchunks"};
  uint32_t offsets[8], lengths[8];
  unsigned chunks, scenarios, def, i;
  unsigned checked_refs = 0;
  int warnings = 0;
  if (size < 256 || memcmp(p, "plgo", 4)) {
    puts("[pkg-diag] WARNING: PlayGo header is short or has unknown magic");
    return 1;
  }
  chunks = diag_le16(p + 10);
  scenarios = diag_le16(p + 14);
  def = diag_le16(p + 20);
  printf("[pkg-diag] PlayGo version=%u.%u images=%u chunks=%u mchunks=%u scenarios=%u default=%u declared_size=%u entry_size=%zu\n",
    diag_le16(p + 4), diag_le16(p + 6), diag_le16(p + 8), chunks,
    diag_le16(p + 12), scenarios, def, diag_le32(p + 16), size);
  if (diag_le32(p + 16) != size || !chunks || !scenarios || def >= scenarios) {
    puts("[pkg-diag] WARNING: PlayGo size/count/default-scenario mismatch");
    warnings++;
  }
  if (strlen(content_id) != 36 || memcmp(p + 64, content_id, 36)) {
    puts("[pkg-diag] WARNING: PlayGo content ID differs from PKG header");
    warnings++;
  } else puts("[pkg-diag] PlayGo content ID matches PKG header");
  for (i = 0; i < 8; i++) {
    offsets[i] = diag_le32(p + 192 + i * 8);
    lengths[i] = diag_le32(p + 196 + i * 8);
    int valid = diag_range(offsets[i], lengths[i], size) &&
                (!lengths[i] || offsets[i] >= 256);
    printf("[pkg-diag] PlayGo %s offset=%u size=%u bounds=%s\n",
           names[i], offsets[i], lengths[i], valid ? "ok" : "INVALID");
    if (!valid) warnings++;
  }
  /* Never follow section offsets until every range has been checked. */
  if (warnings) return warnings;
  if ((uint64_t)scenarios * 32 > lengths[4] ||
      (uint64_t)chunks * 32 > lengths[0] ||
      (uint64_t)diag_le16(p + 12) * 16 > lengths[3]) {
    puts("[pkg-diag] WARNING: PlayGo attribute tables shorter than counts");
    return 1;
  }
  for (i = 0; i < scenarios; i++) {
    const unsigned char *s = p + offsets[4] + i * 32;
    unsigned initial = diag_le16(s + 20), count = diag_le16(s + 22), j;
    uint32_t relative = diag_le32(s + 24);
    if (initial > count || !diag_range(relative, (uint64_t)count * 2, lengths[5])) {
      printf("[pkg-diag] WARNING: scenario %u has invalid chunk list\n", i);
      warnings++;
      continue;
    }
    for (j = 0; j < count; j++) {
      if (++checked_refs > 1000000) {
        puts("[pkg-diag] WARNING: chunk-reference diagnostic limit reached");
        return warnings + 1;
      }
      if (diag_le16(p + offsets[5] + relative + j * 2) >= chunks) {
        printf("[pkg-diag] WARNING: scenario %u references a missing chunk\n", i);
        warnings++;
        break;
      }
    }
  }
  return warnings;
}

static int
diagnose_pkg(const char *path, const char *content_id) {
  unsigned char header[0x80], entry[32];
  struct stat st;
  FILE *fp = fopen(path, "rb");
  uint32_t count, table, i;
  int warnings = 0, playgo_found = 0;
  if (!fp) return -1;
  if (fstat(fileno(fp), &st) || st.st_size < (off_t)sizeof header ||
      fread(header, 1, sizeof header, fp) != sizeof header ||
      memcmp(header, "\x7f" "CNT", 4)) { fclose(fp); return -1; }
  count = diag_be32(header + 16);
  table = diag_be32(header + 24);
  printf("[pkg-diag] flags=0x%08x drm_type=0x%x content_type=0x%x content_flags=0x%08x\n",
    diag_be32(header + 4), diag_be32(header + 0x70),
    diag_be32(header + 0x74), diag_be32(header + 0x78));
  puts("[pkg-diag] retail/FPKG=unknown (header flags alone do not authenticate package type)");
  printf("[pkg-diag] entry_count=%u table_offset=%u\n", count, table);
  if (!count || count > 4096 || table < sizeof header ||
      !diag_range(table, (uint64_t)count * 32, (uint64_t)st.st_size)) {
    puts("[pkg-diag] WARNING: entry table invalid or exceeds diagnostic limit");
    fclose(fp); return 1;
  }
  for (i = 0; i < count; i++) {
    uint32_t id, flags, offset, size;
    if (fseeko(fp, (off_t)((uint64_t)table + i * 32), SEEK_SET) ||
        fread(entry, 1, sizeof entry, fp) != sizeof entry) { warnings++; break; }
    id = diag_be32(entry); flags = diag_be32(entry + 8);
    offset = diag_be32(entry + 16); size = diag_be32(entry + 20);
    int valid = diag_range(offset, size, (uint64_t)st.st_size);
    if (!valid || id == 0x400 || (id >= 0x1000 && id <= 0x1003))
      printf("[pkg-diag] entry=0x%x offset=%u size=%u encrypted=%u bounds=%s\n",
        id, offset, size, !!(flags & 0x80000000u), valid ? "ok" : "INVALID");
    if (!valid) { warnings++; continue; }
    if (id != 0x1001) continue;
    playgo_found++;
    if (flags & 0x80000000u) {
      puts("[pkg-diag] PlayGo encrypted; contents not inspected");
      warnings++; continue;
    }
    if (size < 256 || size > 8 * 1024 * 1024) {
      puts("[pkg-diag] WARNING: PlayGo size outside diagnostic limits");
      warnings++; continue;
    }
    unsigned char *data = malloc(size);
    if (!data) { fclose(fp); return -1; }
    if (fseeko(fp, offset, SEEK_SET) || fread(data, 1, size, fp) != size) warnings++;
    else warnings += diag_playgo(data, size, content_id);
    free(data);
  }
  fclose(fp);
  if (playgo_found != 1) {
    printf("[pkg-diag] WARNING: expected one PlayGo chunk entry, found %d\n", playgo_found);
    warnings++;
  }
  printf("[pkg-diag] complete: warnings=%d (structural checks only, signatures not verified)\n", warnings);
  return warnings;
}
