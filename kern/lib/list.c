#include <list.h>
#include <addrspace.h>

void list_del(struct list_head *entry)
{
    /* struct as_region_metadata *todel = list_entry(entry, struct as_region_metadata, link); */
    __list_del(entry->prev, entry->next);
    /* entry = entry->next; */
    /* as_destroy_region(todel); */
}
