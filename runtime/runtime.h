#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* data;
    int64_t     len;
} rt_string;

rt_string rt_string_concat(rt_string a, rt_string b);
int32_t   rt_string_eq(rt_string a, rt_string b);
int32_t   rt_strlen(rt_string s);

void rt_print_i64(int64_t x);
void rt_print_u64(uint64_t x);
void rt_print_f64(double x);
void rt_print_bool(int32_t b);
void rt_print_string(rt_string s);
void rt_print_char(uint32_t cp);

void rt_println_i64(int64_t x);
void rt_println_u64(uint64_t x);
void rt_println_f64(double x);
void rt_println_bool(int32_t b);
void rt_println_string(rt_string s);
void rt_println_char(uint32_t cp);
void rt_println_empty(void);

rt_string rt_input(void);

#if defined(__GNUC__) || defined(__clang__)
#  define MYC_NORETURN __attribute__((noreturn))
#else
#  define MYC_NORETURN
#endif

void rt_exit(int32_t code) MYC_NORETURN;
void rt_panic(rt_string msg, int32_t line) MYC_NORETURN;

void rt_check_div_zero(int64_t divisor, int32_t line);
void rt_check_bounds(int64_t index, int64_t size, int32_t line);

#ifdef __cplusplus
}
#endif
