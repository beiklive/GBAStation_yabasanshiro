#include "YabLog.h"
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
static FILE* log_file;
void YabLogInit(const char* path) {
  mkdir("sdmc:/GBAStation", 0777); mkdir("sdmc:/GBAStation/log", 0777);
  if (path) log_file = fopen(path, "ab");
  YabLogWrite("log initialized");
}
void YabLogWrite(const char* fmt, ...) {
  char msg[1024]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
  time_t now = time(NULL); struct tm* tmv = localtime(&now); char stamp[32] = "[unknown-time]";
  if (tmv) strftime(stamp, sizeof stamp, "[%Y-%m-%d %H:%M:%S]", tmv);
  if (log_file) { fprintf(log_file, "%s %s\n", stamp, msg); fflush(log_file); }
  printf("%s %s\n", stamp, msg); fflush(stdout);
}
void YabLogShutdown(void) { if (log_file) { YabLogWrite("log shutdown"); fclose(log_file); log_file = NULL; } }
