/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MS912X_TEST_REGISTRY_COMPAT_H
#define MS912X_TEST_REGISTRY_COMPAT_H

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;

#define GFP_KERNEL 0
#define READ_ONCE(value) (value)
#define WRITE_ONCE(value, next) ((value) = (next))
#define container_of(pointer, type, member) \
	((type *)((char *)(pointer) - offsetof(type, member)))
#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)
#define IS_ERR(pointer) ((uintptr_t)(pointer) >= (uintptr_t)-4095)
#define ERR_PTR(error) ((void *)(intptr_t)(error))

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

static inline void INIT_LIST_HEAD(struct list_head *head)
{
	head->next = head;
	head->prev = head;
}

static inline void list_add_tail(struct list_head *entry,
				 struct list_head *head)
{
	entry->prev = head->prev;
	entry->next = head;
	head->prev->next = entry;
	head->prev = entry;
}

static inline void list_del_init(struct list_head *entry)
{
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
	INIT_LIST_HEAD(entry);
}

#define list_for_each_entry(position, head, member) \
	for (position = container_of((head)->next, typeof(*position), member); \
	     &position->member != (head); \
	     position = container_of(position->member.next, typeof(*position), member))

struct test_mutex {
	bool held;
};

static inline void mutex_lock(struct test_mutex *mutex)
{
	assert(!mutex->held);
	mutex->held = true;
}

static inline void mutex_unlock(struct test_mutex *mutex)
{
	assert(mutex->held);
	mutex->held = false;
}

struct drm_device {
	unsigned int refs;
	unsigned int hotplug_events;
	bool unplugged;
};

struct ms912x_device {
	struct drm_device drm;
	struct list_head device_list;
	bool fake_edid_active;
	unsigned int profile_configurations;
};

#endif /* MS912X_TEST_REGISTRY_COMPAT_H */
