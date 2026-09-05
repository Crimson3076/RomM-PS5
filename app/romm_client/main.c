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
#define RECV_BUF_SIZE 65536

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

typedef struct {
  char host[128];
  char port[8];
  char path[256];
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
  strncpy(cfg->path, "/api/roms?limit=1", sizeof cfg->path - 1);

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
    } else if (!strcmp(key, "path")) {
      strncpy(cfg->path, val, sizeof cfg->path - 1);
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


static long
parse_total(const char *body) {
  const char *p = strstr(body, "\"total\":");
  if (p == NULL) {
    return -1;
  }
  return strtol(p + strlen("\"total\":"), NULL, 10);
}


int
main() {
  config_t cfg;
  char credentials[160];
  char auth_b64[256];
  char request[1024];
  char *response;
  size_t response_len;
  size_t response_cap;
  int sock;
  ssize_t n;
  char *status_line_end;
  char *body;
  long total;

  if (load_config(&cfg) != 0) {
    notify("RomM: missing config at %s", CONFIG_PATH);
    return 1;
  }

  if (cfg.host[0] == '\0') {
    notify("RomM: config has no host= line");
    return 1;
  }

  snprintf(credentials, sizeof credentials, "%s:%s", cfg.user, cfg.pass);
  base64_encode(credentials, auth_b64, sizeof auth_b64);

  sock = connect_host(cfg.host, cfg.port);
  if (sock < 0) {
    notify("RomM: could not connect to %s:%s", cfg.host, cfg.port);
    return 1;
  }

  snprintf(request, sizeof request,
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "Authorization: Basic %s\r\n"
           "Connection: close\r\n"
           "\r\n",
           cfg.path, cfg.host, auth_b64);

  if (send(sock, request, strlen(request), 0) < 0) {
    notify("RomM: request send failed");
    close(sock);
    return 1;
  }

  response_cap = RECV_BUF_SIZE;
  response_len = 0;
  response = malloc(response_cap);
  if (response == NULL) {
    close(sock);
    return 1;
  }

  while ((n = recv(sock, response + response_len,
                    response_cap - response_len - 1, 0)) > 0) {
    response_len += (size_t)n;
    if (response_len + 1 >= response_cap) {
      response_cap *= 2;
      response = realloc(response, response_cap);
      if (response == NULL) {
        close(sock);
        return 1;
      }
    }
  }
  close(sock);
  response[response_len] = '\0';

  if (strncmp(response, "HTTP/1.1 200", 12) != 0 &&
      strncmp(response, "HTTP/1.0 200", 12) != 0) {
    status_line_end = strstr(response, "\r\n");
    if (status_line_end != NULL) {
      *status_line_end = '\0';
    }
    notify("RomM: request failed (%s)", response);
    printf("RomM request failed: %s\n", response);
    free(response);
    return 1;
  }

  body = strstr(response, "\r\n\r\n");
  body = (body != NULL) ? body + 4 : response;

  total = parse_total(body);
  if (total >= 0) {
    notify("RomM: %ld games in library", total);
    printf("RomM: %ld games in library\n", total);
  } else {
    notify("RomM: connected, but could not parse response");
    printf("RomM: connected, response was:\n%s\n", body);
  }

  free(response);
  return 0;
}
