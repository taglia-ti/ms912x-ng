// SPDX-License-Identifier: GPL-2.0-only

#include "test_stats_compat.h"

#include <stdio.h>

static u64 fake_time_ns = 1000;

u64 ktime_get_ns(void)
{
	return fake_time_ns;
}

int main(void)
{
	struct ms912x_device first = { 0 };
	struct ms912x_device second = { 0 };

	ms912x_stats_enabled_key = true;
	MS912X_STAT_INC(&first, updates);
	MS912X_STAT_ADD(&first, usb_bytes, 4096);
	MS912X_STAT_INC(&second, updates);
	MS912X_STAT_ADD(&second, usb_bytes, 8192);
	MS912X_STAT_INC(&second, conversion_successes);
	atomic64_set(&second.stats.session_start_ns, 77);

	ms912x_stats_reset(&first);

	assert(atomic64_read(&first.stats.session_start_ns) == 1000);
	assert(atomic64_read(&first.stats.updates) == 0);
	assert(atomic64_read(&first.stats.usb_bytes) == 0);
	assert(atomic64_read(&first.stats.conversion_successes) == 0);
	assert(atomic64_read(&second.stats.updates) == 1);
	assert(atomic64_read(&second.stats.usb_bytes) == 8192);
	assert(atomic64_read(&second.stats.conversion_successes) == 1);
	assert(atomic64_read(&second.stats.session_start_ns) == 77);

	puts("PASS per-adapter stats: reset and updates remain isolated");

	return 0;
}
