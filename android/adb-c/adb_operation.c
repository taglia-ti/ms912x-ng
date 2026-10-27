/* SPDX-License-Identifier: GPL-2.0-only */
#include "adb.h"
#include <time.h>

int64_t adb_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
void adb_operation_init(struct adb_operation *op, const atomic_bool *cancel,
			int timeout_ms)
{
	*op = (struct adb_operation){
		.cancel = cancel,
		.deadline_ms = timeout_ms ? adb_now_ms() + timeout_ms : 0
	};
}
static int fail_as(struct adb_operation *op, const char *error,
		   enum adb_error_kind kind)
{
	if (!op->error) {
		op->error = error;
		op->error_kind = kind;
	}
	return -1;
}
int adb_fail(struct adb_operation *op, const char *error)
{
	return fail_as(op, error, ADB_ERROR_FAILURE);
}
int adb_check(struct adb_operation *op)
{
	if (op->cancel &&
	    atomic_load_explicit(op->cancel, memory_order_acquire))
		return fail_as(op, "ADB operation cancelled", ADB_ERROR_CANCELLED);
	if (op->deadline_ms && adb_now_ms() >= op->deadline_ms)
		return fail_as(op, "ADB operation timed out", ADB_ERROR_TIMEOUT);
	return 0;
}
