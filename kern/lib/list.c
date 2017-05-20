#include <list.h>
#include <addrspace.h>

static void _list_del(struct list_head *prev, struct list_head *next)
{
    next->prev = prev;
    prev->next = next;
}
void list_del(struct list_head *entry)
{
    struct as_region_metadata *todel = list_entry(entry, struct as_region_metadata, link);
    _list_del(entry->prev, entry->next);
    entry = entry->next;
    as_destroy_region(todel);
}
