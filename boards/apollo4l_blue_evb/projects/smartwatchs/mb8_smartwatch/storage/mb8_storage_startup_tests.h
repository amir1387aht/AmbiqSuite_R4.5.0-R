/* SPDX-License-Identifier: GPL-2.0-only
 * TEMPORARY startup diagnostics: remove with its .c or compile tests=0 later.
 */
#ifndef MB8_STORAGE_STARTUP_TESTS_H
#define MB8_STORAGE_STARTUP_TESTS_H
void mb8_storage_startup_raw_tests(void);
void mb8_storage_startup_read_tests(void);
#if WATCH_NAND_TEST_WRITE
void mb8_storage_startup_write_test(void);
#endif
#endif
