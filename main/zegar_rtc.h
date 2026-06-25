#pragma once
#include <stdint.h>
#include <stddef.h>

void zegar_rtc_init(void);
void rtc_get_timestamp(char *buffer, size_t max_len);