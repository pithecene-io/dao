// io.c — Dao runtime IO hooks.
//
// Authority: docs/contracts/CONTRACT_RUNTIME_ABI.md

#include "dao_abi.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void __dao_io_write_stdout(const struct dao_string *msg) {
  if (msg == NULL || msg->ptr == NULL || msg->len <= 0) {
    fputc('\n', stdout);
    return;
  }

  fwrite(msg->ptr, 1, (size_t)msg->len, stdout);
  fputc('\n', stdout);
}

void __dao_io_write_stderr(const struct dao_string *msg) {
  if (msg == NULL || msg->ptr == NULL || msg->len <= 0) {
    fputc('\n', stderr);
    return;
  }

  fwrite(msg->ptr, 1, (size_t)msg->len, stderr);
  fputc('\n', stderr);
}

// Read an entire file into a heap-allocated string. Traps on error.
struct dao_string __dao_io_read_file(const struct dao_string *path) {
  if (path == NULL || path->ptr == NULL || path->len <= 0) {
    fprintf(stderr, "dao: read_file: empty path\n");
    abort();
  }

  // Null-terminate the path for fopen.
  char *cpath = (char *)malloc((size_t)path->len + 1);
  if (cpath == NULL) {
    fprintf(stderr, "dao: read_file: allocation failed\n");
    abort();
  }
  memcpy(cpath, path->ptr, (size_t)path->len);
  cpath[path->len] = '\0';

  FILE *file = fopen(cpath, "rb");
  if (file == NULL) {
    fprintf(stderr, "dao: read_file: cannot open '%s'\n", cpath);
    free(cpath);
    abort();
  }

  // Get file size.
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (size < 0) {
    fprintf(stderr, "dao: read_file: cannot determine size of '%s'\n", cpath);
    fclose(file);
    free(cpath);
    abort();
  }

  if (size == 0) {
    fclose(file);
    free(cpath);
    return (struct dao_string){.ptr = NULL, .len = 0};
  }

  char *buf = (char *)malloc((size_t)size);
  if (buf == NULL) {
    fprintf(stderr, "dao: read_file: allocation failed for '%s'\n", cpath);
    fclose(file);
    free(cpath);
    abort();
  }

  size_t read_bytes = fread(buf, 1, (size_t)size, file);
  fclose(file);
  free(cpath);

  return (struct dao_string){.ptr = buf, .len = (int64_t)read_bytes};
}

// Write a string to a file. Returns true on success, false on failure.
bool __dao_io_write_file(const struct dao_string *path,
                          const struct dao_string *content) {
  if (path == NULL || path->ptr == NULL || path->len <= 0) {
    return false;
  }

  char *cpath = (char *)malloc((size_t)path->len + 1);
  if (cpath == NULL) {
    return false;
  }
  memcpy(cpath, path->ptr, (size_t)path->len);
  cpath[path->len] = '\0';

  FILE *file = fopen(cpath, "wb");
  if (file == NULL) {
    free(cpath);
    return false;
  }

  size_t written = 0;
  if (content != NULL && content->ptr != NULL && content->len > 0) {
    written = fwrite(content->ptr, 1, (size_t)content->len, file);
  }

  fclose(file);
  free(cpath);

  return content == NULL || content->len <= 0 ||
         written == (size_t)content->len;
}

// Check if a file exists.
bool __dao_io_file_exists(const struct dao_string *path) {
  if (path == NULL || path->ptr == NULL || path->len <= 0) {
    return false;
  }

  char *cpath = (char *)malloc((size_t)path->len + 1);
  if (cpath == NULL) {
    return false;
  }
  memcpy(cpath, path->ptr, (size_t)path->len);
  cpath[path->len] = '\0';

  struct stat st;
  bool exists = (stat(cpath, &st) == 0);
  free(cpath);
  return exists;
}
