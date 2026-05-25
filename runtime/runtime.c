#define _POSIX_C_SOURCE 200809L

#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die_with(const char* what, int32_t line) {
    fflush(stdout);
    fprintf(stderr, "runtime error: %s at line %d\n", what, line);
    exit(1);
}

rt_string rt_string_concat(rt_string a, rt_string b) {
    int64_t total = a.len + b.len;
    char* buf = (char*)malloc((size_t)total + 1);
    if (buf == NULL) {
        die_with("out of memory", 0);
    }
    if (a.len > 0) memcpy(buf, a.data, (size_t)a.len);
    if (b.len > 0) memcpy(buf + a.len, b.data, (size_t)b.len);
    buf[total] = '\0';

    rt_string r;
    r.data = buf;
    r.len  = total;
    return r;
}

int32_t rt_string_eq(rt_string a, rt_string b) {
    if (a.len != b.len) return 0;
    if (a.len == 0)     return 1;
    return memcmp(a.data, b.data, (size_t)a.len) == 0 ? 1 : 0;
}

int32_t rt_strlen(rt_string s) {
    return (int32_t)s.len;
}

void rt_print_i64(int64_t x)    { printf("%lld", (long long)x); }
void rt_print_u64(uint64_t x)   { printf("%llu", (unsigned long long)x); }
void rt_print_f64(double x)     { printf("%g", x); }
void rt_print_bool(int32_t b)   { fputs(b ? "true" : "false", stdout); }
void rt_print_string(rt_string s) {
    if (s.len > 0) fwrite(s.data, 1, (size_t)s.len, stdout);
}

void rt_println_i64(int64_t x)    { rt_print_i64(x);    putchar('\n'); }
void rt_println_u64(uint64_t x)   { rt_print_u64(x);    putchar('\n'); }
void rt_println_f64(double x)     { rt_print_f64(x);    putchar('\n'); }
void rt_println_bool(int32_t b)   { rt_print_bool(b);   putchar('\n'); }
void rt_println_string(rt_string s) { rt_print_string(s); putchar('\n'); }
void rt_println_empty(void)       { putchar('\n'); }

rt_string rt_input(void) {
    char*  line = NULL;
    size_t cap  = 0;
    ssize_t n = getline(&line, &cap, stdin);

    rt_string r;
    if (n < 0) {
        free(line);
        r.data = "";
        r.len  = 0;
        return r;
    }
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
        line[--n] = '\0';
    }
    r.data = line;
    r.len  = (int64_t)n;
    return r;
}

void rt_exit(int32_t code) {
    fflush(stdout);
    exit((int)code);
}

void rt_panic(rt_string msg, int32_t line) {
    fflush(stdout);
    if (msg.len > 0) {
        fprintf(stderr, "runtime error: %.*s at line %d\n",
                (int)msg.len, msg.data, (int)line);
    } else {
        fprintf(stderr, "runtime error: panic at line %d\n", (int)line);
    }
    exit(1);
}

void rt_check_div_zero(int64_t divisor, int32_t line) {
    if (divisor == 0) {
        die_with("division by zero", line);
    }
}

void rt_check_bounds(int64_t index, int64_t size, int32_t line) {
    if (index < 0 || index >= size) {
        die_with("index out of bounds", line);
    }
}
