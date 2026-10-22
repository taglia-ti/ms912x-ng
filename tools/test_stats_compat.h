/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_TEST_STATS_COMPAT_H
#define MS912X_TEST_STATS_COMPAT_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

typedef uint64_t u64;
typedef int64_t s64;
typedef long long atomic64_t;

#define MS912X_H

#define DECLARE_STATIC_KEY_FALSE(name) extern bool name
#define DEFINE_STATIC_KEY_FALSE(name) bool name

static inline bool static_branch_unlikely(const bool *key)
{
	return *key;
}

static inline void atomic64_inc(atomic64_t *value)
{
	++*value;
}

static inline void atomic64_add(u64 amount, atomic64_t *value)
{
	*value += amount;
}

static inline void atomic64_set(atomic64_t *value, long long next)
{
	*value = next;
}

static inline s64 atomic64_read(const atomic64_t *value)
{
	return *value;
}

static inline s64 atomic64_cmpxchg(atomic64_t *value, s64 old, s64 next)
{
	s64 previous = *value;

	if (previous == old)
		*value = next;
	return previous;
}

u64 ktime_get_ns(void);

#include "ms912x_stats.h"

struct ms912x_device {
	struct ms912x_stats stats;
};

#endif /* MS912X_TEST_STATS_COMPAT_H */
