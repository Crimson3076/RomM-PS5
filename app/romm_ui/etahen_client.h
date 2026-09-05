/* etaHEN DPI v2: form POST /upload, port 12800, text SUCCESS:/FAILED:.
 * Source: etaHEN/etaHEN, Source Code/util/source/DirectPKGInstaller.cpp.
 * Pass the existing local path as url, not the package bytes. No retries:
 * a lost response can mean that etaHEN already submitted the installation.
 */
#ifndef ETAHEN_DPI_PORT
#define ETAHEN_DPI_PORT 12800
#endif
#ifndef ETAHEN_TIMEOUT_SECONDS
#define ETAHEN_TIMEOUT_SECONDS 30
#endif

static int
etahen_install(const char *path, const char *name, char *result, size_t result_size) {
  char encoded_path[1804], encoded_name[772], form[2700];
  char request[3200], response[8192];
  size_t sent = 0, used = 0;
  int sock = -1, rc = -1, status = 0, len;
  struct sockaddr_in addr = {0};
  struct timeval timeout = {ETAHEN_TIMEOUT_SECONDS, 0};
  ssize_t n;
  snprintf(result, result_size,
    "etaHEN DPI v2 unavailable. Enable it in etaHEN (port 12800). PKG saved.");
  if (strlen(path) > 599 || strlen(name) > 255) {
    snprintf(result, result_size, "PKG path too long for etaHEN request. PKG saved.");
    return -1;
  }
  url_encode(path, encoded_path, sizeof encoded_path);
  url_encode(name, encoded_name, sizeof encoded_name);
  len = snprintf(form, sizeof form, "url=%s&content_name=%s", encoded_path, encoded_name);
  if (len < 0 || (size_t)len >= sizeof form) return -1;
  len = snprintf(request, sizeof request,
    "POST /upload HTTP/1.0\r\nHost: 127.0.0.1:%d\r\n"
    "Content-Type: application/x-www-form-urlencoded\r\n"
    "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
    ETAHEN_DPI_PORT, strlen(form), form);
  if (len < 0 || (size_t)len >= sizeof request) return -1;
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) goto done;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) ||
      setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout)) goto done;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(ETAHEN_DPI_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(sock, (struct sockaddr *)&addr, sizeof addr)) goto done;
  printf("[etahen] POST /upload on 127.0.0.1:%d saved_path=%s\n", ETAHEN_DPI_PORT, path);
  /* From here on, an interrupted request must not be reported as rejected. */
  rc = -2;
  snprintf(result, result_size,
    "etaHEN response unconfirmed. Check etaHEN notifications before retrying. PKG saved.");
  while (sent < (size_t)len) {
    n = send(sock, request + sent, (size_t)len - sent, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) goto done;
    sent += (size_t)n;
  }
  /* HTTP/1.0 + Connection: close avoids chunked encoding. Read a bounded
     complete response, never treating HTTP 200 alone as installation success. */
  while (used < sizeof response - 1) {
    n = recv(sock, response + used, sizeof response - 1 - used, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) goto done;
    if (!n) break;
    used += (size_t)n;
  }
  if (used == sizeof response - 1) goto done;
  response[used] = '\0';
  char *body = strstr(response, "\r\n\r\n");
  if (!body || sscanf(response, "HTTP/%*u.%*u %d", &status) != 1) goto done;
  *body = '\0';
  body += 4;
  size_t body_size = used - (size_t)(body - response);
  for (char *line = strstr(response, "\r\n"); line; ) {
    line += 2;
    if (!strncasecmp(line, "Transfer-Encoding:", 18)) goto done;
    if (!strncasecmp(line, "Content-Length:", 15)) {
      char *end;
      errno = 0;
      unsigned long long declared = strtoull(line + 15, &end, 10);
      if (errno || end == line + 15 || (*end && *end != '\r') || declared != body_size) goto done;
    }
    line = strstr(line, "\r\n");
  }
  /* Keep server-provided output on one log line. */
  for (size_t i = 0; i < body_size; i++)
    if ((unsigned char)body[i] < 32) body[i] = ' ';
  printf("[etahen] HTTP %d response=%.1024s\n", status, body);
  if (status == 200 && !strncmp(body, "SUCCESS:", 8)) {
    snprintf(result, result_size,
      "etaHEN accepted the installation request. Check PS5 notifications for completion. PKG saved.");
    rc = 0;
  } else if (status == 200 && !strncmp(body, "FAILED:", 7)) {
    snprintf(result, result_size, "etaHEN rejected installation: %.160s. PKG saved.", body + 7);
    rc = 1;
  }
done:
  if (sock >= 0) close(sock);
  printf("[etahen] result=%d %s\n", rc, result);
  return rc;
}
