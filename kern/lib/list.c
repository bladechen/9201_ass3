#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include "list.h"

struct list* init_list(size_t offset)
{
    struct list* l = kmalloc(sizeof(struct list));
    if (l == NULL)
    {
        return NULL;
    }
    l->offset = offset;
    link_init(&(l->head));
    return l;
}

bool is_list_empty(const struct list* l)
{
    KASSERT(l != NULL);
    return list_empty(&(l->head));
}
void destroy_list(struct list* l)
{
    KASSERT(is_list_empty(l));
    kfree(l);
    return;
}
void make_list_empty(struct list * l)
{
    struct list_head* pos = NULL;
    struct list_head* tmp = NULL;
    list_for_each_safe(pos, tmp, &(l->head))
    {
        if (pos == NULL)
        {
            break;
        }
        list_del(pos);
        pos->owner = NULL;
    }
    return;
}
 void list_insert_head(struct list* l, void* entry)
 {
     struct     list_head* tmp = list_get_link_from_entry(l, entry);
     list_add(tmp, &(l->head));
     tmp->owner = l;
     return;
 }
void list_insert_tail(struct list *l, void* entry)
{
    struct list_head* tmp = list_get_link_from_entry(l, entry);
    list_add_tail(tmp,  &(l->head));
    tmp->owner = l;
    return;


}
void* list_head(struct list* l)
{
    struct list_head * tmp = link_next(&l->head);
    return (void * )((size_t)tmp - l->offset);

}
struct list_head* link_next(struct list_head* link)
{
    KASSERT(link != NULL);
    return link->next;
}


/* void* link_prev(struct list_head* link) */
/* { */
/*  */
/* } */

/* void link_detach1(void* entry, struct list* l) */
/* { */
/*      struct     list_head* tmp = list_get_link_from_entry(l, entry); */
/*      list_del(tmp); */
/*      tmp->owner = NULL; */
/*      return; */
/* } */

/* void link_detach2(void* entry, int offset) */
/* { */
/*     struct list_head* tmp = (struct list_head*) ((size_t) entry + offset); */
/*     list_del(tmp); */
/*      tmp->owner = NULL; */
/*     return; */
/* } */

void link_init(struct list_head* link)
{
    link->owner = NULL ;
    INIT_LIST_HEAD(link);
    return;
}
bool is_linked(struct list_head* link)
{
    return !(link->next == link->prev && (link->next == link ||link->next == NULL) && link->owner == NULL);

}
/* void __list_del(struct list_head *prev, struct list_head *next) */
/* { */
/*     next->prev = prev; */
/*     prev->next = next; */
/* } */
void list_del(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
    entry->next = 0;
    entry->prev = 0;
}
