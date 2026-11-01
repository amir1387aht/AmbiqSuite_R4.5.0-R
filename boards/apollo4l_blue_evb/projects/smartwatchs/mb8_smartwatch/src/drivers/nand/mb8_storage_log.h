/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MB8_STORAGE_LOG_H
#define MB8_STORAGE_LOG_H
/* Task-context only; private bounded buffer, shared existing UART sink mutex. */
void     mb8_storage_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
unsigned mb8_storage_log_dropped(void);
#endif
