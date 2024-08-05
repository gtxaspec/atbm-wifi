/**************************************************************************************************************
 * altobeam RTOS wifi hmac source code 
 *
 * Copyright (c) 2018, altobeam.inc   All rights reserved.
 *
 *  The source code contains proprietary information of AltoBeam, and shall not be distributed, 
 *  copied, reproduced, or disclosed in whole or in part without prior written permission of AltoBeam.
*****************************************************************************************************************/
#ifndef __LIST_H__
#define __LIST_H__

#ifdef LINUX_OS
#include <linux/list.h>
#endif

/*
 * These are non-NULL pointers that will result in page faults
 * under normal circumstances, used to verify that nobody uses
 * non-initialized list entries.
 */
 #ifndef LINUX_OS
#define LIST_POISON1  ((atbm_void*) 0x00100100)
#define LIST_POISON2  ((atbm_void*) 0x00200200)
#endif

#ifndef offsetof
//#define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE*)0)->MEMBER)
#define offsetof(type, member) ((size_t)( (char *)&(((type *)0)->member) - (char *)0 ))
#endif
//1, changed from GCC to normal c, 2, delete type check.
#define atbm_container_of(ptr, type, member)   (type*)((char*)(ptr) - offsetof(type,member))
/*
 * Simple doubly linked list implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole lists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */

struct atbm_list_head {
	struct atbm_list_head *next, *prev;
};

#define atbm_LIST_HEAD_INIT(name) { &(name), &(name) }

#define atbm_LIST_HEAD(name) \
	struct atbm_list_head name = atbm_LIST_HEAD_INIT(name)

#define ATBM_INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static __INLINE atbm_void atbm___list_add(struct atbm_list_head *new,
			      struct atbm_list_head *prev,
			      struct atbm_list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

/**
 * atbm_list_add - add a new entry
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static __INLINE atbm_void atbm_list_add(struct atbm_list_head *new, struct atbm_list_head *head)
{
	atbm___list_add(new, head, head->next);
}

/**
 * atbm_list_add_tail - add a new entry
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static __INLINE atbm_void atbm_list_add_tail(struct atbm_list_head *new, struct atbm_list_head *head)
{
	atbm___list_add(new, head->prev, head);
}


/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static __INLINE atbm_void atbm___list_del(struct atbm_list_head * prev, struct atbm_list_head * next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * atbm_list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: atbm_list_empty on entry does not return ATBM_TRUE after this, the entry is
 * in an undefined state.
 */
static __INLINE atbm_void atbm_list_del(struct atbm_list_head *entry)
{
	atbm___list_del(entry->prev, entry->next);
	entry->next = LIST_POISON1;
	entry->prev = LIST_POISON2;
}

/**
 * atbm_list_del_init - deletes entry from list and reinitialize it.
 * @entry: the element to delete from the list.
 */
static __INLINE atbm_void atbm_list_del_init(struct atbm_list_head *entry) {

	atbm___list_del(entry->prev, entry->next);
	ATBM_INIT_LIST_HEAD(entry);
}

/**
 * atbm_list_move - delete from one list and add as another's head
 * @list: the entry to move
 * @head: the head that will precede our entry
 */
static __INLINE atbm_void atbm_list_move(struct atbm_list_head *list, struct atbm_list_head *head) {

        atbm___list_del(list->prev, list->next);
        atbm_list_add(list, head);
}

/**
 * atbm_list_move_tail - delete from one list and add as another's tail
 * @list: the entry to move
 * @head: the head that will follow our entry
 */
static __INLINE atbm_void atbm_list_move_tail(struct atbm_list_head *list, struct atbm_list_head *head) {

        atbm___list_del(list->prev, list->next);
        atbm_list_add_tail(list, head);
}

/**
 * atbm_list_empty - tests whether a list is empty
 * @head: the list to test.
 */
static __INLINE int atbm_list_empty(const struct atbm_list_head *head)
{
	return head->next == head;
}

/**
 * atbm_list_empty_careful - tests whether a list is
 * empty _and_ checks that no other CPU might be
 * in the process of still modifying either member
 *
 * NOTE: using atbm_list_empty_careful() without synchronization
 * can only be safe if the only activity that can happen
 * to the list entry is atbm_list_del_init(). Eg. it cannot be used
 * if another CPU could re-atbm_list_add() it.
 *
 * @head: the list to test.
 */
static __INLINE int atbm_list_empty_careful(const struct atbm_list_head *head)
{
	struct atbm_list_head *next = head->next;
	return (next == head) && (next == head->prev);
}

static __INLINE atbm_void __atbm_list_splice(struct atbm_list_head *list,
				 struct atbm_list_head *head)
{
	struct atbm_list_head *first = list->next;
	struct atbm_list_head *last = list->prev;
	struct atbm_list_head *at = head->next;

	first->prev = head;
	head->next = first;

	last->next = at;
	at->prev = last;
}

/**
 * atbm_list_splice - join two lists
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static __INLINE atbm_void atbm_list_splice(struct atbm_list_head *list, struct atbm_list_head *head)
{
	if (!atbm_list_empty(list))
		__atbm_list_splice(list, head);
}

/**
 * atbm_list_splice_init - join two lists and reinitialise the emptied list.
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 *
 * The list at @list is reinitialised
 */
static __INLINE atbm_void atbm_list_splice_init(struct atbm_list_head *list,
				    struct atbm_list_head *head)
{
	if (!atbm_list_empty(list)) {
		__atbm_list_splice(list, head);
		ATBM_INIT_LIST_HEAD(list);
	}
}

static __INLINE int atbm_list_swap(struct atbm_list_head *a, struct atbm_list_head *b, struct atbm_list_head *list)
{
        if (a->next == list || b->prev == list)
                return -1;

        atbm_list_del(a);
        atbm_list_add(a, b);

	return 0;
}
/**
 * atbm_list_entry - get the struct for this entry
 * @ptr:	the &struct atbm_list_head pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_struct within the struct.
 */
#define atbm_list_entry(ptr, type, member) \
		atbm_container_of(ptr, type, member)

/**
 * atbm_list_for_each	-	iterate over a list
 * @pos:	the &struct atbm_list_head to use as a loop counter.
 * @head:	the head for your list.
 */
#define atbm_list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)


/**
 * atbm_list_for_each_prev	-	iterate over a list backwards
 * @pos:	the &struct atbm_list_head to use as a loop counter.
 * @head:	the head for your list.
 */
#define atbm_list_for_each_prev(pos, head) \
	do{										\
		for (pos = (head)->prev; pos != (head); pos = pos->prev) \
	}while(0)

/**
 * atbm_list_for_each_safe	-	iterate over a list safe against removal of list entry
 * @pos:	the &struct atbm_list_head to use as a loop counter.
 * @n:		another &struct atbm_list_head to use as temporary storage
 * @head:	the head for your list.
 */
#define atbm_list_for_each_safe(pos, n, head) \
	do{											\
		for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next) \
	}while(0)

/**
 * list_for_each_entry	-	iterate over list of given type
 * @pos:	the type * to use as a loop counter.
 * @head:	the head for your list.
 * @member:	the name of the list_struct within the struct.
 */
#define atbm_list_for_each_entry(pos, head1, member) \
	do{												\
		for (pos = atbm_list_entry((head1)->next, typeof(*pos), member); &(pos)->member != (head1); 	\
		     pos = atbm_list_entry((pos)->member.next, typeof(*pos), member));       \
	}while(0)

/**
 * atbm_list_for_each_entry_reverse - iterate backwards over list of given type.
 * @pos:	the type * to use as a loop counter.
 * @head:	the head for your list.
 * @member:	the name of the list_struct within the struct.
 */
#define atbm_list_for_each_entry_reverse(pos, head, member)			\
	do{																\
		for (pos = atbm_list_entry((head)->prev, typeof(*pos), member); &pos->member != (head); 	\
		     pos = atbm_list_entry(pos->member.prev, typeof(*pos), member))   \
	}while(0)

/**
 * atbm_list_prepare_entry - prepare a pos entry for use as a start point in
 *			atbm_list_for_each_entry_continue
 * @pos:	the type * to use as a start point
 * @head:	the head of the list
 * @member:	the name of the list_struct within the struct.
 */
#define atbm_list_prepare_entry(pos, head, member) \
	do{								\
		((pos) ? : atbm_list_entry(head, typeof(*pos), member))  \
	}while(0)

/**
 * atbm_list_for_each_entry_continue -	iterate over list of given type
 *			continuing after existing point
 * @pos:	the type * to use as a loop counter.
 * @head:	the head for your list.
 * @member:	the name of the list_struct within the struct.
 */
#define atbm_list_for_each_entry_continue(pos, head, member) 		\
	for (pos = atbm_list_entry(pos->member.next, typeof(*pos), member); &pos->member != (head);	\
	     pos = atbm_list_entry(pos->member.next, typeof(*pos), member))

/**
 * atbm_list_for_each_entry_safe - iterate over list of given type safe against removal of list entry
 * @pos:	the type * to use as a loop counter.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_struct within the struct.
 */
#define atbm_list_for_each_entry_safe(pos, n, head, member,type)			\
	for (pos = atbm_list_entry((head)->next, type, member),	\
		n = atbm_list_entry(pos->member.next, type, member);	\
	     &pos->member != (head); 					\
	     pos = n, n = atbm_list_entry(n->member.next, type, member))

/**
 * atbm_list_for_each_entry_reverse_safe - iterate over list of given type safe against removal of list entry
 * @pos:	the type * to use as a loop counter.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_struct within the struct.
 */
#define atbm_list_for_each_entry_reverse_safe(pos, n, head, member)		\
	for (pos = atbm_list_entry((head)->prev, typeof(*pos), member),	\
		n = atbm_list_entry(pos->member.prev, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = n, n = atbm_list_entry(n->member.prev, typeof(*n), member))
	     


#define atbm_list_first_entry(ptr,type,member) \
				atbm_list_entry((ptr)->next,type,member)



static __INLINE atbm_void atbm_list_splice_tail_init(struct atbm_list_head *list,struct atbm_list_head *head)
{
	if(!atbm_list_empty(list)){
		__atbm_list_splice(list,head->prev);
		ATBM_INIT_LIST_HEAD(list);
	}
}
#endif /* __LIST_H__ */
