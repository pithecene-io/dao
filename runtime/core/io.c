// io.c — Dao runtime IO hooks.
//
// Authority: docs/contracts/CONTRACT_RUNTIME_ABI.md

#include "dao_abi.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Convert a dao_string to a null-terminated C string on the heap.
// Caller must free() the returned pointer. Returns NULL on allocation
// failure (does not abort — callers decide their own failure policy).
static char *dao_string_to_cstr(const struct dao_string *str) {
  char *cstr = (char *)malloc((size_t)str->len + 1);
  if (cstr == NULL) {
    return NULL;
  }
  memcpy(cstr, str->ptr, (size_t)str->len);
  cstr[str->len] = '\0';
  return cstr;
}

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

  char *cpath = dao_string_to_cstr(path);
  if (cpath == NULL) {
    fprintf(stderr, "dao: read_file: allocation failed\n");
    abort();
  }

  FILE *file = fopen(cpath, "rb");
  if (file == NULL) {
    fprintf(stderr, "dao: read_file: cannot open '%s'\n", cpath);
    free(cpath);
    abort();
  }

  // Get file size via stat (works for files > 2GB, unlike fseek/ftell
  // which use long and fail on 32-bit platforms).
  struct stat file_stat;
  if (fstat(fileno(file), &file_stat) != 0) {
    fprintf(stderr, "dao: read_file: cannot stat '%s'\n", cpath);
    fclose(file);
    free(cpath);
    abort();
  }
  int64_t size = (int64_t)file_stat.st_size;

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
  int read_err = ferror(file);
  fclose(file);

  if (read_err || read_bytes != (size_t)size) {
    fprintf(stderr, "dao: read_file: read error for '%s' "
            "(expected %" PRId64 " bytes, got %zu)\n",
            cpath, size, read_bytes);
    free(buf);
    free(cpath);
    abort();
  }

  free(cpath);
  return (struct dao_string){.ptr = buf, .len = (int64_t)read_bytes};
}

// Write a string to a file. Returns true on success, false on failure.
bool __dao_io_write_file(const struct dao_string *path,
                          const struct dao_string *content) {
  if (path == NULL || path->ptr == NULL || path->len <= 0) {
    return false;
  }

  char *cpath = dao_string_to_cstr(path);
  if (cpath == NULL) {
    return false;
  }

  FILE *file = fopen(cpath, "wb");
  if (file == NULL) {
    free(cpath);
    return false;
  }

  size_t written = 0;
  if (content != NULL && content->ptr != NULL && content->len > 0) {
    written = fwrite(content->ptr, 1, (size_t)content->len, file);
  }

  // fclose flushes buffered data — check its return value to catch
  // disk-full or deferred write errors that fwrite did not report.
  int close_err = fclose(file);
  free(cpath);

  if (close_err != 0) {
    return false;
  }
  return content == NULL || content->len <= 0 ||
         written == (size_t)content->len;
}

// Check if a file exists.
bool __dao_io_file_exists(const struct dao_string *path) {
  if (path == NULL || path->ptr == NULL || path->len <= 0) {
    return false;
  }

  char *cpath = dao_string_to_cstr(path);
  if (cpath == NULL) {
    return false;
  }

  struct stat st;
  bool exists = (stat(cpath, &st) == 0);
  free(cpath);
  return exists;
}
