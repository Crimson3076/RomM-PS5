/* Copyright (C) 2026

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <strings.h>
#include <sys/time.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define CONFIG_PATH "/data/romm-ps5/config.txt"
#define UI_PORT "8081"
#define ROMS_PER_PAGE 20
#define RECV_BUF_SIZE 65536
#define REQ_BUF_SIZE 2048
#define DOWNLOAD_DIR "/data/romm-ps5/downloads"

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

typedef struct pkg_metadata {
  const char *uri;
  const char *ex_uri;
  const char *playgo_scenario_id;
  const char *content_id;
  const char *content_name;
  const char *icon_url;
} pkg_metadata_t;

typedef struct pkg_info {
  char content_id[48];
  int type;
  int platform;
} pkg_info_t;

typedef struct playgo_info {
  char lang[8][30];
  char scenario_ids[3][64];
  char content_ids[64];
  long unknown[810];
} playgo_info_t;

int sceAppInstUtilInitialize(void);
int sceAppInstUtilInstallByPackage(const pkg_metadata_t*, pkg_info_t*,
                                    playgo_info_t*);

typedef struct {
  char host[128];
  char port[8];
  char user[64];
  char pass[64];
} config_t;


static void
notify(const char *fmt, ...) {
  notify_request_t req;
  va_list ap;

  bzero(&req, sizeof req);
  va_start(ap, fmt);
  vsnprintf(req.message, sizeof req.message, fmt, ap);
  va_end(ap);

  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}


static int
load_config(config_t *cfg) {
  FILE *fp;
  char line[512];
  char *eq;
  char *key;
  char *val;
  size_t len;

  memset(cfg, 0, sizeof *cfg);
  strncpy(cfg->port, "8080", sizeof cfg->port - 1);

  fp = fopen(CONFIG_PATH, "r");
  if (fp == NULL) {
    return -1;
  }

  while (fgets(line, sizeof line, fp) != NULL) {
    len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
      line[--len] = '\0';
    }

    eq = strchr(line, '=');
    if (eq == NULL) {
      continue;
    }
    *eq = '\0';
    key = line;
    val = eq + 1;

    if (!strcmp(key, "host")) {
      strncpy(cfg->host, val, sizeof cfg->host - 1);
    } else if (!strcmp(key, "port")) {
      strncpy(cfg->port, val, sizeof cfg->port - 1);
    } else if (!strcmp(key, "user")) {
      strncpy(cfg->user, val, sizeof cfg->user - 1);
    } else if (!strcmp(key, "pass")) {
      strncpy(cfg->pass, val, sizeof cfg->pass - 1);
    }
  }

  fclose(fp);
  return 0;
}


static void
base64_encode(const char *in, char *out, size_t out_size) {
  static const char tbl[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t in_len = strlen(in);
  size_t i;
  size_t o = 0;
  unsigned int val;
  int bits;

  val = 0;
  bits = 0;
  for (i = 0; i < in_len; i++) {
    val = (val << 8) | (unsigned char)in[i];
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      if (o + 1 >= out_size) {
        goto done;
      }
      out[o++] = tbl[(val >> bits) & 0x3F];
    }
  }
  if (bits > 0) {
    if (o + 1 < out_size) {
      out[o++] = tbl[(val << (6 - bits)) & 0x3F];
    }
  }
  while (o % 4 != 0) {
    if (o + 1 >= out_size) {
      break;
    }
    out[o++] = '=';
  }

done:
  out[o < out_size ? o : out_size - 1] = '\0';
}


static int
connect_host(const char *host, const char *port) {
  struct addrinfo hints;
  struct addrinfo *res;
  struct addrinfo *rp;
  int sock;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(host, port, &hints, &res) != 0) {
    return -1;
  }

  sock = -1;
  for (rp = res; rp != NULL; rp = rp->ai_next) {
    sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock < 0) {
      continue;
    }
    if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
      break;
    }
    close(sock);
    sock = -1;
  }

  freeaddrinfo(res);
  return sock;
}


/* Fetch a path from the RomM server; returns malloc'd body (past headers)
   on HTTP 200, or NULL. The returned pointer must be freed via
   free(*out_alloc). */
static char *
romm_get(const config_t *cfg, const char *auth_b64, const char *path,
         char **out_alloc) {
  char request[512];
  char *response;
  size_t response_len;
  size_t response_cap;
  int sock;
  ssize_t n;
  char *body;

  sock = connect_host(cfg->host, cfg->port);
  if (sock < 0) {
    *out_alloc = NULL;
    return NULL;
  }

  snprintf(request, sizeof request,
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "Authorization: Basic %s\r\n"
           "Connection: close\r\n"
           "\r\n",
           path, cfg->host, auth_b64);

  if (send(sock, request, strlen(request), 0) < 0) {
    close(sock);
    *out_alloc = NULL;
    return NULL;
  }

  response_cap = RECV_BUF_SIZE;
  response_len = 0;
  response = malloc(response_cap);
  if (response == NULL) {
    close(sock);
    *out_alloc = NULL;
    return NULL;
  }

  while ((n = recv(sock, response + response_len,
                    response_cap - response_len - 1, 0)) > 0) {
    response_len += (size_t)n;
    if (response_len + 1 >= response_cap) {
      response_cap *= 2;
      response = realloc(response, response_cap);
      if (response == NULL) {
        close(sock);
        *out_alloc = NULL;
        return NULL;
      }
    }
  }
  close(sock);
  response[response_len] = '\0';

  if (strncmp(response, "HTTP/1.1 200", 12) != 0 &&
      strncmp(response, "HTTP/1.0 200", 12) != 0) {
    free(response);
    *out_alloc = NULL;
    return NULL;
  }

  body = strstr(response, "\r\n\r\n");
  body = (body != NULL) ? body + 4 : response;

  *out_alloc = response;
  return body;
}


/* Percent-encode a single path segment for use in an HTTP request path. */
static void
url_encode(const char *in, char *out, size_t out_size) {
  static const char hex[] = "0123456789ABCDEF";
  const unsigned char *p;
  size_t oi = 0;

  for (p = (const unsigned char *)in; *p != '\0' && oi + 4 < out_size; p++) {
    unsigned char c = *p;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out[oi++] = (char)c;
    } else {
      out[oi++] = '%';
      out[oi++] = hex[(c >> 4) & 0xF];
      out[oi++] = hex[c & 0xF];
    }
  }
  out[oi < out_size ? oi : out_size - 1] = '\0';
}


/* Require a framed, complete response before exposing a file to the installer.
   HTTP/1.0 requests avoid chunked transfer encoding; reject it if sent anyway. */
static int
romm_download_to_file(const config_t *cfg, const char *auth_b64,
                       const char *path, const char *dest_path) {
  char request[2048], header[16384], buf[65536], part[1024];
  size_t used = 0;
  unsigned long long expected = 0, received = 0;
  int have_length = 0, status = 0, ok = 0, sock = -1, part_created = 0;
  FILE *fp = NULL;
  const char *failure = "connection failed";
  struct timeval timeout = {60, 0};
  int len;
  ssize_t n;

  len = snprintf(part, sizeof part, "%s.part", dest_path);
  if (len < 0 || (size_t)len >= sizeof part) return -1;
  len = snprintf(request, sizeof request,
                 "GET %s HTTP/1.0\r\nHost: %s:%s\r\n"
                 "Authorization: Basic %s\r\n"
                 "Accept-Encoding: identity\r\nConnection: close\r\n\r\n",
                 path, cfg->host, cfg->port, auth_b64);
  if (len < 0 || (size_t)len >= sizeof request) return -1;
  sock = connect_host(cfg->host, cfg->port);
  if (sock < 0) goto done;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) ||
      setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout)) {
    failure = "could not set network timeout";
    goto done;
  }
  while (used < (size_t)len) {
    n = send(sock, request + used, (size_t)len - used, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { failure = "request send failed"; goto done; }
    used += (size_t)n;
  }
  /* Read only the header here, so no body bytes can be dropped. */
  used = 0;
  while (used < sizeof header - 1) {
    n = recv(sock, header + used, 1, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { failure = "incomplete HTTP headers"; goto done; }
    used++;
    if (used >= 4 && !memcmp(header + used - 4, "\r\n\r\n", 4)) break;
  }
  header[used] = '\0';
  if (used < 4 || memcmp(header + used - 4, "\r\n\r\n", 4)) {
    failure = "HTTP headers too large"; goto done;
  }
  if (sscanf(header, "HTTP/%*u.%*u %d", &status) != 1 || status != 200) {
    failure = "server did not return HTTP 200"; goto done;
  }
  {
    char *line = strstr(header, "\r\n") + 2;
    while (*line != '\r') {
      char *end = strstr(line, "\r\n");
      *end = '\0';
      if (!strncasecmp(line, "Content-Length:", 15)) {
        char *value = line + 15, *tail;
        unsigned long long length;
        while (*value == ' ' || *value == '\t') value++;
        errno = 0;
        length = strtoull(value, &tail, 10);
        while (*tail == ' ' || *tail == '\t') tail++;
        if (*value < '0' || *value > '9' || errno || *tail ||
            (have_length && length != expected)) {
          failure = "invalid Content-Length"; goto done;
        }
        expected = length;
        have_length = 1;
      } else if (!strncasecmp(line, "Transfer-Encoding:", 18)) {
        failure = "unsupported Transfer-Encoding"; goto done;
      } else if (!strncasecmp(line, "Content-Encoding:", 17)) {
        char *value = line + 17;
        while (*value == ' ' || *value == '\t') value++;
        if (strcasecmp(value, "identity")) {
          failure = "unsupported Content-Encoding"; goto done;
        }
      }
      line = end + 2;
    }
  }
  if (!have_length || expected == 0) {
    failure = "missing or empty Content-Length"; goto done;
  }
  printf("[download] HTTP %d, expected=%llu bytes\n", status, expected);
  fflush(stdout);
  fp = fopen(part, "wb");
  if (!fp) { failure = "cannot open temporary download file"; goto done; }
  part_created = 1;
  while (received < expected) {
    size_t want = expected - received > sizeof buf ? sizeof buf :
                  (size_t)(expected - received);
    n = recv(sock, buf, want, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { failure = "transfer interrupted or timed out"; goto done; }
    if (fwrite(buf, 1, (size_t)n, fp) != (size_t)n) {
      failure = "disk write failed (check free space)"; goto done;
    }
    received += (unsigned long long)n;
  }
  if (fclose(fp)) {
    fp = NULL; failure = "disk flush failed"; goto done;
  }
  fp = NULL;
  if (rename(part, dest_path)) {
    failure = "cannot finalize download"; goto done;
  }
  ok = 1;
done:
  if (fp) fclose(fp);
  if (sock >= 0) close(sock);
  if (!ok) {
    /* Only remove a temporary file created by this attempt. */
    if (part_created) remove(part);
    printf("[download] FAILED: %s (HTTP %d, received=%llu, expected=%llu)\n",
           failure, status, received, expected);
  }
  fflush(stdout);
  return ok ? 0 : -1;
}


/* Copy a JSON string value starting right after its opening quote,
   unescaping \" and \\, until the closing quote or end of string. */
static void
copy_json_string(const char *value_start, char *out, size_t out_size) {
  const char *v = value_start;
  size_t oi = 0;

  while (*v != '\0' && *v != '"') {
    if (*v == '\\' && *(v + 1) != '\0') {
      v++;
    }
    if (oi + 1 < out_size) {
      out[oi++] = *v;
    }
    v++;
  }
  out[oi < out_size ? oi : out_size - 1] = '\0';
}


static void
html_escape(const char *in, char *out, size_t out_size) {
  size_t oi = 0;
  const char *p;

  for (p = in; *p != '\0' && oi + 6 < out_size; p++) {
    switch (*p) {
    case '&':  oi += snprintf(out + oi, out_size - oi, "&amp;");  break;
    case '<':  oi += snprintf(out + oi, out_size - oi, "&lt;");   break;
    case '>':  oi += snprintf(out + oi, out_size - oi, "&gt;");   break;
    case '"':  oi += snprintf(out + oi, out_size - oi, "&quot;"); break;
    default:   out[oi++] = *p; break;
    }
  }
  out[oi < out_size ? oi : out_size - 1] = '\0';
}


static long
parse_query_long(const char *request_line, const char *key, long fallback) {
  char needle[32];
  const char *q;

  snprintf(needle, sizeof needle, "%s=", key);
  q = strstr(request_line, needle);
  if (q == NULL) {
    return fallback;
  }
  return strtol(q + strlen(needle), NULL, 10);
}


/* Find the innermost '{' enclosing the byte at pos, by scanning backward
   and tracking brace depth. */
static const char *
find_object_start(const char *body, const char *pos) {
  const char *p = pos;
  int depth = 0;

  while (p > body) {
    p--;
    if (*p == '}') {
      depth++;
    } else if (*p == '{') {
      if (depth == 0) {
        return p;
      }
      depth--;
    }
  }
  return NULL;
}


/* Given a pointer to the '{' that opens an object, find the matching '}'. */
static const char *
find_object_end(const char *obj_start) {
  const char *p = obj_start;
  int depth = 0;

  while (*p != '\0') {
    if (*p == '{') {
      depth++;
    } else if (*p == '}') {
      depth--;
      if (depth == 0) {
        return p;
      }
    }
    p++;
  }
  return NULL;
}


/* Bounded substring search: like strstr, but never reads past end. */
static const char *
find_in_range(const char *start, const char *end, const char *key) {
  size_t key_len = strlen(key);
  const char *p;

  if (key_len == 0 || end < start || (size_t)(end - start) < key_len) {
    return NULL;
  }

  for (p = start; p <= end - key_len; p++) {
    if (memcmp(p, key, key_len) == 0) {
      return p;
    }
  }
  return NULL;
}


static long
find_int_field_in_range(const char *start, const char *end, const char *key) {
  const char *p = find_in_range(start, end, key);
  if (p == NULL) {
    return -1;
  }
  return strtol(p + strlen(key), NULL, 10);
}


/* Find "key":"..." within [start,end) and copy its unescaped value into
   out. Writes an empty string and returns -1 if not found. */
static int
find_string_field_in_range(const char *start, const char *end,
                            const char *key, char *out, size_t out_size) {
  const char *p = find_in_range(start, end, key);
  if (p == NULL) {
    out[0] = '\0';
    return -1;
  }
  copy_json_string(p + strlen(key), out, out_size);
  return 0;
}


/* Look up the numeric platform_id whose platform_slug matches slug, by
   scanning /api/platforms. Returns -1 if not found or on error. */
static long
lookup_platform_id(const config_t *cfg, const char *auth_b64,
                    const char *slug) {
  char *alloc = NULL;
  char *body;
  char needle[64];
  const char *match;
  const char *obj_start;
  const char *obj_end;
  long id;

  body = romm_get(cfg, auth_b64, "/api/platforms", &alloc);
  if (body == NULL) {
    return -1;
  }

  snprintf(needle, sizeof needle, "\"slug\":\"%s\"", slug);
  match = strstr(body, needle);
  if (match == NULL) {
    free(alloc);
    return -1;
  }

  obj_start = find_object_start(body, match);
  obj_end = obj_start != NULL ? find_object_end(obj_start) : NULL;
  if (obj_start == NULL || obj_end == NULL) {
    free(alloc);
    return -1;
  }

  id = find_int_field_in_range(obj_start, obj_end, "\"id\":");
  free(alloc);
  return id;
}


static void
send_all(int fd, const char *buf, size_t len) {
  size_t sent = 0;
  ssize_t n;
  while (sent < len) {
    n = send(fd, buf + sent, len - sent, 0);
    if (n <= 0) {
      return;
    }
    sent += (size_t)n;
  }
}


static void
serve_picker(int client_fd, const config_t *cfg, const char *auth_b64) {
  long ps4_id;
  long ps5_id;
  char html[4096];
  size_t html_len;
  char header[256];
  int header_len;

  ps4_id = lookup_platform_id(cfg, auth_b64, "ps4");
  ps5_id = lookup_platform_id(cfg, auth_b64, "ps5");

  html_len = snprintf(html, sizeof html,
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<title>RomM</title>"
    "<style>"
    "body{background:#111;color:#eee;font-family:sans-serif;font-size:3vw}"
    "a{color:#8cf;display:block;padding:2vw;text-decoration:none}"
    "a:focus,a:hover{background:#8cf;color:#111}"
    "</style></head><body>"
    "<h1>Select Platform</h1>");

  if (ps4_id >= 0) {
    html_len += snprintf(html + html_len, sizeof html - html_len,
      "<a href=\"/?platform_id=%ld&offset=0\" tabindex=\"0\">PlayStation 4</a>",
      ps4_id);
  }
  if (ps5_id >= 0) {
    html_len += snprintf(html + html_len, sizeof html - html_len,
      "<a href=\"/?platform_id=%ld&offset=0\" tabindex=\"0\">PlayStation 5</a>",
      ps5_id);
  }
  if (ps4_id < 0 && ps5_id < 0) {
    html_len += snprintf(html + html_len, sizeof html - html_len,
      "<p>No PS4 or PS5 platform found on this RomM server.</p>");
  }
  html_len += snprintf(html + html_len, sizeof html - html_len,
    "</body></html>");

  header_len = snprintf(header, sizeof header,
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/html; charset=utf-8\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n"
                         "\r\n", html_len);
  send_all(client_fd, header, (size_t)header_len);
  send_all(client_fd, html, html_len);
  close(client_fd);
}


static void
serve_page(int client_fd, const config_t *cfg, const char *auth_b64,
           long platform_id, int offset) {
  char api_path[160];
  char *alloc = NULL;
  char *body;
  const char *cursor;
  char fs_name[256];
  char platform[128];
  char fs_name_esc[512];
  char platform_esc[256];
  long rom_id;
  char *html;
  size_t html_cap = 65536;
  size_t html_len = 0;
  long total;
  int shown;

  html = malloc(html_cap);
  if (html == NULL) {
    close(client_fd);
    return;
  }

#define APPEND(...) \
  html_len += snprintf(html + html_len, html_cap - html_len, __VA_ARGS__)

  snprintf(api_path, sizeof api_path,
           "/api/roms?limit=%d&offset=%d&platform_ids=%ld",
           ROMS_PER_PAGE, offset, platform_id);
  body = romm_get(cfg, auth_b64, api_path, &alloc);

  APPEND("<!doctype html><html><head><meta charset=\"utf-8\">"
         "<title>RomM</title>"
         "<style>"
         "body{background:#111;color:#eee;font-family:sans-serif;font-size:2vw}"
         "a{color:#8cf;display:block;padding:1vw;text-decoration:none}"
         "a:focus,a:hover{background:#8cf;color:#111}"
         "nav{margin-top:2vw}nav a{display:inline-block}"
         "</style></head><body>"
         "<h1>RomM Library</h1>"
         "<p><a href=\"/\" tabindex=\"0\">&laquo; Change platform</a></p><ul>");

  if (body == NULL) {
    APPEND("<li>Could not reach RomM server.</li>");
    total = 0;
  } else {
    cursor = body;
    shown = 0;
    for (;;) {
      const char *fs_match;
      const char *obj_start;
      const char *obj_end;

      if (shown >= ROMS_PER_PAGE) {
        break;
      }
      fs_match = strstr(cursor, "\"fs_name\":\"");
      if (fs_match == NULL) {
        break;
      }
      obj_start = find_object_start(body, fs_match);
      obj_end = obj_start != NULL ? find_object_end(obj_start) : NULL;
      if (obj_start == NULL || obj_end == NULL) {
        break;
      }

      copy_json_string(fs_match + strlen("\"fs_name\":\""),
                        fs_name, sizeof fs_name);
      find_string_field_in_range(obj_start, obj_end,
                                  "\"platform_display_name\":\"",
                                  platform, sizeof platform);
      rom_id = find_int_field_in_range(obj_start, obj_end, "\"id\":");

      html_escape(fs_name, fs_name_esc, sizeof fs_name_esc);
      html_escape(platform, platform_esc, sizeof platform_esc);
      APPEND("<li><a href=\"/rom?id=%ld&platform_id=%ld&offset=%d\" "
             "tabindex=\"0\">%s <small>(%s)</small></a></li>",
             rom_id, platform_id, offset, fs_name_esc, platform_esc);
      shown++;
      cursor = obj_end + 1;
      if (html_cap - html_len < 1024) {
        break;
      }
    }
    if (shown == 0) {
      APPEND("<li>No games found.</li>");
    }

    total = 0;
    {
      const char *tp = strstr(body, "\"total\":");
      if (tp != NULL) {
        total = strtol(tp + strlen("\"total\":"), NULL, 10);
      }
    }
    free(alloc);
  }

  APPEND("</ul><nav>");
  if (offset > 0) {
    int prev = offset - ROMS_PER_PAGE;
    if (prev < 0) {
      prev = 0;
    }
    APPEND("<a href=\"/?platform_id=%ld&offset=%d\" tabindex=\"0\">&laquo; Prev</a> ",
           platform_id, prev);
  }
  if (offset + ROMS_PER_PAGE < total) {
    APPEND("<a href=\"/?platform_id=%ld&offset=%d\" tabindex=\"0\">Next &raquo;</a>",
           platform_id, offset + ROMS_PER_PAGE);
  }
  APPEND("</nav></body></html>");

#undef APPEND

  {
    char header[256];
    int header_len = snprintf(header, sizeof header,
                               "HTTP/1.1 200 OK\r\n"
                               "Content-Type: text/html; charset=utf-8\r\n"
                               "Content-Length: %zu\r\n"
                               "Connection: close\r\n"
                               "\r\n", html_len);
    send_all(client_fd, header, (size_t)header_len);
    send_all(client_fd, html, html_len);
  }

  free(html);
  close(client_fd);
}


static void
send_html_page(int client_fd, const char *html, size_t html_len) {
  char header[256];
  int header_len = snprintf(header, sizeof header,
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/html; charset=utf-8\r\n"
                             "Content-Length: %zu\r\n"
                             "Connection: close\r\n"
                             "\r\n", html_len);
  send_all(client_fd, header, (size_t)header_len);
  send_all(client_fd, html, html_len);
}


static void
format_size(long bytes, char *out, size_t out_size) {
  static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  double val;
  int u = 0;

  if (bytes < 0) {
    snprintf(out, out_size, "unknown");
    return;
  }
  val = (double)bytes;
  while (val >= 1024.0 && u < 4) {
    val /= 1024.0;
    u++;
  }
  snprintf(out, out_size, "%.1f %s", val, units[u]);
}


static void
serve_rom_details(int client_fd, const config_t *cfg, const char *auth_b64,
                   long rom_id, long platform_id, int offset) {
  char api_path[64];
  char *alloc = NULL;
  char *body;
  char fs_name[256];
  char platform[128];
  char fs_name_esc[512];
  char platform_esc[256];
  char size_str[32];
  long size_bytes;
  char html[4096];
  size_t html_len;
  const char *p;

  snprintf(api_path, sizeof api_path, "/api/roms/%ld", rom_id);
  body = romm_get(cfg, auth_b64, api_path, &alloc);

  if (body == NULL) {
    html_len = snprintf(html, sizeof html,
      "<!doctype html><html><head><meta charset=\"utf-8\">"
      "<title>RomM</title>"
      "<style>body{background:#111;color:#eee;font-family:sans-serif;"
      "font-size:2.2vw}a{color:#8cf}</style></head><body>"
      "<h1>Could not reach RomM server.</h1>"
      "<p><a href=\"/?platform_id=%ld&offset=%d\" tabindex=\"0\">"
      "&laquo; Back</a></p></body></html>", platform_id, offset);
    send_html_page(client_fd, html, html_len);
    close(client_fd);
    return;
  }

  p = strstr(body, "\"fs_name\":\"");
  if (p != NULL) {
    copy_json_string(p + strlen("\"fs_name\":\""), fs_name, sizeof fs_name);
  } else {
    strncpy(fs_name, "Unknown", sizeof fs_name - 1);
    fs_name[sizeof fs_name - 1] = '\0';
  }

  p = strstr(body, "\"platform_display_name\":\"");
  if (p != NULL) {
    copy_json_string(p + strlen("\"platform_display_name\":\""),
                      platform, sizeof platform);
  } else {
    platform[0] = '\0';
  }

  p = strstr(body, "\"fs_size_bytes\":");
  size_bytes = (p != NULL) ?
    strtol(p + strlen("\"fs_size_bytes\":"), NULL, 10) : -1;

  free(alloc);

  html_escape(fs_name, fs_name_esc, sizeof fs_name_esc);
  html_escape(platform, platform_esc, sizeof platform_esc);
  format_size(size_bytes, size_str, sizeof size_str);

  html_len = snprintf(html, sizeof html,
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<title>RomM</title>"
    "<style>"
    "body{background:#111;color:#eee;font-family:sans-serif;font-size:2.2vw}"
    "a{color:#8cf;display:block;padding:1.5vw;text-decoration:none}"
    "a:focus,a:hover{background:#8cf;color:#111}"
    "</style></head><body>"
    "<h1>%s</h1>"
    "<p>Platform: %s</p>"
    "<p>Size: %s</p>"
    "<a href=\"/download?id=%ld&platform_id=%ld&offset=%d\" tabindex=\"0\">Download</a>"
    "<a href=\"/?platform_id=%ld&offset=%d\" tabindex=\"0\">&laquo; Back to list</a>"
    "</body></html>",
    fs_name_esc, platform_esc, size_str,
    rom_id, platform_id, offset,
    platform_id, offset);

  send_html_page(client_fd, html, html_len);
  close(client_fd);
}


static void
serve_unsupported_platform(int client_fd, long rom_id, long platform_id,
                            int offset) {
  char html[1024];
  size_t html_len = snprintf(html, sizeof html,
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<title>RomM</title>"
    "<style>body{background:#111;color:#eee;font-family:sans-serif;"
    "font-size:2.2vw}"
    "a{color:#8cf;display:block;padding:1.5vw;text-decoration:none}"
    "a:focus,a:hover{background:#8cf;color:#111}</style></head><body>"
    "<h1>Download not implemented for this platform yet</h1>"
    "<p>Only PS4 titles support automatic download+install right now. "
    "PS5 zip extraction is a separate, harder future milestone.</p>"
    "<a href=\"/rom?id=%ld&platform_id=%ld&offset=%d\" tabindex=\"0\">"
    "&laquo; Back</a></body></html>", rom_id, platform_id, offset);
  send_html_page(client_fd, html, html_len);
  close(client_fd);
}


static void
serve_error_page(int client_fd, const char *message, long rom_id,
                  long platform_id, int offset) {
  char html[2048];
  char message_esc[512];
  size_t html_len;

  html_escape(message, message_esc, sizeof message_esc);
  html_len = snprintf(html, sizeof html,
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<title>RomM</title>"
    "<style>body{background:#111;color:#eee;font-family:sans-serif;"
    "font-size:2.2vw}"
    "a{color:#8cf;display:block;padding:1.5vw;text-decoration:none}"
    "a:focus,a:hover{background:#8cf;color:#111}</style></head><body>"
    "<h1>%s</h1>"
    "<a href=\"/rom?id=%ld&platform_id=%ld&offset=%d\" tabindex=\"0\">"
    "&laquo; Back</a></body></html>", message_esc, rom_id, platform_id,
    offset);
  send_html_page(client_fd, html, html_len);
  close(client_fd);
}


static void
serve_download(int client_fd, const config_t *cfg, const char *auth_b64,
               long rom_id, long platform_id, int offset) {
  long ps4_id;
  char api_path[64];
  char *alloc = NULL;
  char *body;
  const char *p;
  char fs_name[256];
  char fs_name_esc[512];
  char html[2048];
  size_t html_len;

  ps4_id = lookup_platform_id(cfg, auth_b64, "ps4");
  if (ps4_id < 0 || platform_id != ps4_id) {
    serve_unsupported_platform(client_fd, rom_id, platform_id, offset);
    return;
  }

  snprintf(api_path, sizeof api_path, "/api/roms/%ld", rom_id);
  body = romm_get(cfg, auth_b64, api_path, &alloc);
  if (body == NULL) {
    serve_error_page(client_fd, "Could not reach RomM server.",
                      rom_id, platform_id, offset);
    return;
  }

  p = strstr(body, "\"fs_name\":\"");
  if (p != NULL) {
    copy_json_string(p + strlen("\"fs_name\":\""), fs_name, sizeof fs_name);
  } else {
    fs_name[0] = '\0';
  }
  free(alloc);

  if (fs_name[0] == '\0') {
    serve_error_page(client_fd, "Could not determine the rom's file name.",
                      rom_id, platform_id, offset);
    return;
  }

  if (strchr(fs_name, '/') || strchr(fs_name, '\\') ||
      strlen(fs_name) < 5 || strcasecmp(fs_name + strlen(fs_name) - 4, ".pkg")) {
    serve_error_page(client_fd, "Select a single PS4 .pkg file, not a folder or archive.",
                    rom_id, platform_id, offset);
    return;
  }

  html_escape(fs_name, fs_name_esc, sizeof fs_name_esc);

  {
    char encoded_name[768];
    char content_path[800];
    char dest_path[600];
    int rc;

    url_encode(fs_name, encoded_name, sizeof encoded_name);
    snprintf(content_path, sizeof content_path,
             "/api/roms/%ld/content/%s", rom_id, encoded_name);

    mkdir("/data/romm-ps5", 0777);
    mkdir(DOWNLOAD_DIR, 0777);
    snprintf(dest_path, sizeof dest_path, "%s/%s", DOWNLOAD_DIR, fs_name);

    notify("RomM: downloading %s ...", fs_name);
    printf("[download] GET %s -> %s\n", content_path, dest_path);
    fflush(stdout);
    rc = romm_download_to_file(cfg, auth_b64, content_path, dest_path);
    if (rc != 0) {
      notify("RomM: download failed for %s", fs_name);
      printf("[download] FAILED for %s\n", dest_path);
      fflush(stdout);
      serve_error_page(client_fd, "Download failed.",
                        rom_id, platform_id, offset);
      return;
    }
    printf("[download] complete: %s\n", dest_path);
    fflush(stdout);

    notify("RomM: download complete, installing %s ...", fs_name);

    {
      pkg_metadata_t metainfo;
      pkg_info_t pkginfo;
      playgo_info_t playgoinfo;
      int err;
      int i;

      memset(&metainfo, 0, sizeof metainfo);
      memset(&pkginfo, 0, sizeof pkginfo);
      memset(&playgoinfo, 0, sizeof playgoinfo);

      metainfo.uri = dest_path;
      metainfo.ex_uri = "";
      metainfo.playgo_scenario_id = "";
      metainfo.content_id = "";
      metainfo.icon_url = "";
      metainfo.content_name = dest_path;
      for (i = 0; dest_path[i] != '\0'; i++) {
        if (dest_path[i] == '/') {
          metainfo.content_name = dest_path + i + 1;
        }
      }

      printf("[install] uri=%s content_name=%s\n",
             metainfo.uri, metainfo.content_name);
      fflush(stdout);

      err = sceAppInstUtilInitialize();
      printf("[install] sceAppInstUtilInitialize -> 0x%x\n", err);
      fflush(stdout);
      if (err != 0) {
        notify("RomM: sceAppInstUtilInitialize failed: 0x%x", err);
      } else {
        err = sceAppInstUtilInstallByPackage(&metainfo, &pkginfo,
                                              &playgoinfo);
        printf("[install] sceAppInstUtilInstallByPackage -> 0x%x\n", err);
        fflush(stdout);
        if (err == 0) {
          notify("RomM: install started for %s", fs_name);
        } else {
          notify("RomM: install failed: 0x%x", err);
        }
      }
      if (err != 0) {
        char message[160];
        snprintf(message, sizeof message,
                 "PKG downloaded, but installation failed (0x%x). The file is retained.", err);
        serve_error_page(client_fd, message, rom_id, platform_id, offset);
        return;
      }
    }
  }

  html_len = snprintf(html, sizeof html,
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<title>RomM</title>"
    "<style>body{background:#111;color:#eee;font-family:sans-serif;"
    "font-size:2.2vw}"
    "a{color:#8cf;display:block;padding:1.5vw;text-decoration:none}"
    "a:focus,a:hover{background:#8cf;color:#111}</style></head><body>"
    "<h1>%s</h1>"
    "<p>Downloaded and install triggered. Check the PS5 notifications "
    "for the result.</p>"
    "<a href=\"/rom?id=%ld&platform_id=%ld&offset=%d\" tabindex=\"0\">"
    "&laquo; Back</a></body></html>", fs_name_esc, rom_id, platform_id,
    offset);
  send_html_page(client_fd, html, html_len);
  close(client_fd);
}


int
main() {
  config_t cfg;
  char credentials[160];
  char auth_b64[256];
  int listen_fd;
  struct sockaddr_in addr;
  int optval = 1;

  setvbuf(stdout, NULL, _IONBF, 0);

  if (load_config(&cfg) != 0 || cfg.host[0] == '\0') {
    return 1;
  }

  snprintf(credentials, sizeof credentials, "%s:%s", cfg.user, cfg.pass);
  base64_encode(credentials, auth_b64, sizeof auth_b64);

  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    return 1;
  }
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((unsigned short)atoi(UI_PORT));

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
    close(listen_fd);
    return 1;
  }
  if (listen(listen_fd, 8) != 0) {
    close(listen_fd);
    return 1;
  }

  notify("RomM: open the PS5 Browser and go to http://127.0.0.1:" UI_PORT "/");

  for (;;) {
    int client_fd = accept(listen_fd, NULL, NULL);
    char req[REQ_BUF_SIZE];
    ssize_t n;
    char *line_end;
    char path[32];
    long platform_id;
    long offset;
    long rom_id;

    if (client_fd < 0) {
      continue;
    }

    n = recv(client_fd, req, sizeof req - 1, 0);
    if (n <= 0) {
      close(client_fd);
      continue;
    }
    req[n] = '\0';

    line_end = strstr(req, "\r\n");
    if (line_end != NULL) {
      *line_end = '\0';
    }

    path[0] = '\0';
    if (strncmp(req, "GET ", 4) == 0) {
      const char *p = req + 4;
      size_t i = 0;
      while (*p != '\0' && *p != ' ' && *p != '?' && i + 1 < sizeof path) {
        path[i++] = *p++;
      }
      path[i] = '\0';
    }

    platform_id = parse_query_long(req, "platform_id", -1);
    offset = parse_query_long(req, "offset", 0);
    rom_id = parse_query_long(req, "id", -1);

    if (strcmp(path, "/rom") == 0 && rom_id >= 0) {
      serve_rom_details(client_fd, &cfg, auth_b64, rom_id, platform_id,
                         (int)offset);
    } else if (strcmp(path, "/download") == 0 && rom_id >= 0) {
      serve_download(client_fd, &cfg, auth_b64, rom_id, platform_id,
                      (int)offset);
    } else if (platform_id < 0) {
      serve_picker(client_fd, &cfg, auth_b64);
    } else {
      serve_page(client_fd, &cfg, auth_b64, platform_id, (int)offset);
    }
  }

  close(listen_fd);
  return 0;
}
