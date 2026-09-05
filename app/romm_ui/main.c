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
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define CONFIG_PATH "/data/romm-ps5/config.txt"
#define UI_PORT "8081"
#define ROMS_PER_PAGE 20
#define RECV_BUF_SIZE 65536
#define REQ_BUF_SIZE 2048

int sceUserServiceInitialize(void*);
int sceUserServiceTerminate(void);
int sceSystemServiceLaunchWebBrowser(const char *uri, void*);

typedef struct {
  char host[128];
  char port[8];
  char user[64];
  char pass[64];
} config_t;


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


/* Find the next occurrence of "key":"..." starting at *cursor, copy the
   unescaped value into out, and advance *cursor past it. Returns 0 on
   success, -1 if not found. */
static int
extract_field(const char **cursor, const char *key,
              char *out, size_t out_size) {
  const char *p;
  const char *v;
  size_t oi;

  p = strstr(*cursor, key);
  if (p == NULL) {
    return -1;
  }
  v = p + strlen(key);

  oi = 0;
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

  *cursor = (*v == '"') ? v + 1 : v;
  return 0;
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
           "/api/roms?limit=%d&offset=%d&platform_id=%ld",
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
    while (shown < ROMS_PER_PAGE &&
           extract_field(&cursor, "\"fs_name\":\"",
                         fs_name, sizeof fs_name) == 0) {
      const char *plat_cursor = cursor;
      if (extract_field(&plat_cursor, "\"platform_display_name\":\"",
                        platform, sizeof platform) != 0) {
        platform[0] = '\0';
      }
      html_escape(fs_name, fs_name_esc, sizeof fs_name_esc);
      html_escape(platform, platform_esc, sizeof platform_esc);
      APPEND("<li><a href=\"#\" tabindex=\"0\">%s <small>(%s)</small></a></li>",
             fs_name_esc, platform_esc);
      shown++;
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


int
main() {
  config_t cfg;
  char credentials[160];
  char auth_b64[256];
  int listen_fd;
  struct sockaddr_in addr;
  int optval = 1;

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

  if (sceUserServiceInitialize(0) != 0) {
    close(listen_fd);
    return 1;
  }
  sceSystemServiceLaunchWebBrowser("http://127.0.0.1:" UI_PORT "/", 0);

  for (;;) {
    int client_fd = accept(listen_fd, NULL, NULL);
    char req[REQ_BUF_SIZE];
    ssize_t n;
    char *line_end;
    long platform_id;
    long offset;

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
    platform_id = parse_query_long(req, "platform_id", -1);
    offset = parse_query_long(req, "offset", 0);

    if (platform_id < 0) {
      serve_picker(client_fd, &cfg, auth_b64);
    } else {
      serve_page(client_fd, &cfg, auth_b64, platform_id, (int)offset);
    }
  }

  sceUserServiceTerminate();
  close(listen_fd);
  return 0;
}
