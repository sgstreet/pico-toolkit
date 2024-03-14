/*
 * Copyright (C) 2017 Red Rocket Computing, LLC
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * linked-list.h
 *
 * Created on: Feb 7, 2017
 *     Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#ifndef LINKED_LIST_H_
#define LINKED_LIST_H_

#include <stdbool.h>
#include <assert.h>

#include "container-of.h"

struct linked_list
{
	struct linked_list *next;
	struct linked_list *prev;
};

#define LIST_INIT(list) {&list, &list}

#define list_pop_entry(list, type, member) container_of_or_null(list_pop(list), type, member)

#define list_entry(ptr, type, member) container_of_or_null(ptr, type, member)
#define list_first_entry(list, type, member) list_entry((list)->next, type, member)
#define list_last_entry(list, type, member) list_entry((list)->prev, type, member)
#define list_next_entry(position, member) list_entry((position)->member.next, typeof(*(position)), member)
#define list_prev_entry(position, member) list_entry((position)->member.prev, typeof(*(position)), member)

#define list_for_each(cursor, list) \
	for (cursor = (list)->next; cursor != (list); cursor = cursor->next)

#define list_for_each_mutable(cursor, next, list) \
	for (cursor = (list)->next, next = cursor->next; cursor != (list); cursor = next, next = cursor->next)

#define list_for_each_entry(cursor, list, member) \
	for (cursor = list_first_entry(list, typeof(*cursor), member); &cursor->member != (list); cursor = list_next_entry(cursor, member))

#define list_for_each_entry_mutable(cursor, next, list, member) \
	for (cursor = list_first_entry(list, typeof(*cursor), member), next = list_next_entry(cursor, member); &cursor->member != (list); cursor = next, next = list_next_entry(next, member))

static inline void list_init(struct linked_list *list)
{
	assert(list != 0);

	list->next = list;
	list->prev = list;
}

static inline bool list_is_empty(const struct linked_list *list)
{
	assert(list != 0);

	return list->next == list && list->prev == list;
}

static inline void list_insert_after(struct linked_list *entry, struct linked_list *node)
{
	assert(entry != 0 && node != 0);

	node->next = entry->next;
	node->prev = entry;
	entry->next->prev = node;
	entry->next = node;
}

static inline void list_insert_before(struct linked_list *entry, struct linked_list *node)
{
	assert(entry != 0 && node != 0);

	node->next = entry;
	node->prev = entry->prev;
	entry->prev->next = node;
	entry->prev = node;
}

static inline void list_remove(struct linked_list *node)
{
	assert(node != 0 && node->next != 0 && node->prev != 0);

	node->next->prev = node->prev;
	node->prev->next = node->next;
	list_init(node);
}

static inline struct linked_list *list_front(const struct linked_list *list)
{
	assert(list != 0);

	if (list_is_empty(list))
		return 0;
	return list->next;
}

static inline struct linked_list *list_back(const struct linked_list *list)
{
	assert(list != 0);

	if (list_is_empty(list))
		return 0;
	return list->prev;
}

static inline void list_add(struct linked_list *list, struct linked_list *node)
{
	assert(list != 0 && node != 0);

	node->next = list;
	node->prev = list->prev;
	list->prev->next = node;
	list->prev = node;
}

static inline void list_push(struct linked_list *list, struct linked_list *node)
{
	assert(list != 0 && node != 0);

	node->next = list->next;
	node->prev = list;
	list->next->prev = node;
	list->next = node;
}

static inline struct linked_list *list_pop(struct linked_list *list)
{
	assert(list != 0);

	struct linked_list *front = 0;
	if (!list_is_empty(list))
	{
		front = list->next;
		list_remove(front);
	}
	return front;
}

static inline struct linked_list *list_next(const struct linked_list *list, struct linked_list *node)
{
	assert(list != 0 && node != 0);

	if (node->next == list)
		return 0;
	return node->next;
}

static inline struct linked_list *list_prev(const struct linked_list *list, struct linked_list *node)
{
	assert(list != 0 && node != 0);

	if (node->prev == list)
		return 0;
	return node->prev;
}

static inline int list_is_linked(struct linked_list *node)
{
	assert(node != 0);

	return node->next != node && node->prev != node;
}

static inline size_t list_size(struct linked_list *list)
{
	assert(list != 0);

	size_t size = 0;
	struct linked_list *cursor;
	list_for_each(cursor, list)
		++size;
	return size;
}

#endif
