#ifndef SEMISYNC_CLIENT_UTILITY
#define SEMISYNC_CLIENT_UTILITY
#ifdef MYSQL_CLIENT
#include <stdarg.h>
#include <stdio.h>
static void sql_print_information(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  fprintf(stderr, "Info: ");
  fprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
}

static void sql_print_warning(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  fprintf(stderr, "Warning: ");
  fprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
}

static void sql_print_error(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  fprintf(stderr, "Error: ");
  fprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
}
#endif
#endif
