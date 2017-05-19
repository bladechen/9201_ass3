#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
/* #include <frametable.h> */
/* #include <coreswap.h> */

/* Place your page table functions here */



struct lock *vm_lock = NULL;

void vm_bootstrap(void)
{

    /* vm_lock = lock_create("vm_lock"); */
    /* if (lock == NULL) */
    /* { */
    /*     panic("vm lock create failed!\n"); */
    /*  */
    /* } */

    /* init_coreswap(); */
    DEBUG(DB_VM, "init_frametable ing....\n");
    init_frametable();
    DEBUG(DB_VM, "init_frametable finish\n");
    /* vaddr_t p = alloc_kpages(1); */
    /* DEBUG(DB_VM, "alloc 0x%x\n", p); */
    /*  */
    /* vaddr_t x = alloc_kpages(1); */
    /* DEBUG(DB_VM, "alloc 0x%x\n", x); */
    /*  */
    /*  */
    /* free_kpages(p); */
    /*  */
    /* free_kpages(x); */
    /*  */
    /* x = alloc_kpages(1); */
    /* DEBUG(DB_VM, "alloc 0x%x\n", x); */
    /* free_kpages(x); */



    return;

}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    (void) faulttype;
    (void) faultaddress;

    panic("vm_fault hasn't been written yet\n");

    return EFAULT;
}

/*
 *
 * SMP-specific functions.  Unused in our configuration.
 */

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}

/* int do_swapout(struct pagetable_entry* entry) */
/* { */
/*     lock_acquire(&vm_lock); */
/*     KASSERT(entry != NULL); */
/*     KASSERT(entry->status == PAGETABLE_ENTRY_INRAM); */
/*     KASSERT(entry->swappage_entry == NULL); */
/*     KASSERT(entry->corepage_entry != NULL); */
/*     KASSERT(entry->corepage_entry->status == COREPAGE_USER); */
/*     KASSERT(entry->corepage_entry->owner == entry); */
/*     int ret = swapout_corepage(entry->corepage_entry, &(entry->swappage_entry)); */
/*     if (ret != 0) */
/*     { */
/*         lock_release(&vm_lock); */
/*         return ret; */
/*     } */
/*  */
/*     entry->corepage_entry = NULL; */
/*     entry->status = PAGETABLE_ENTRY_INSWAP; */
/*     struct pagetable_entry* tmp = entry->next; */
/*     struct swap_page* swap = entry->swappage_entry; */
/*  */
/*     while (tmp != entry) */
/*     { */
/*         tmp->corepage_entry = NULL; */
/*         tmp->swappage_entry = swap; */
/*         tmp->status = PAGETABLE_ENTRY_INSWAP; */
/*     } */
/*  */
/*     lock_release(&vm_lock); */
/*     return 0; */
/* } */
/*  */
/* int do_swapin(struct pagetable_entry* entry) */
/* { */
/*     KASSERT(entry != NULL); */
/*     KASSERT(entry->status == PAGETABLE_ENTRY_INSWAP); */
/*     KASSERT(entry->swappage_entry != NULL); */
/*     KASSERT(entry->corepage_entry == NULL); */
/*     KASSERT(entry->swappage_entry->owner == entry); */
/*     KASSERT(entry->swappage_entry->status == SWAP_INUSE); */
/*  */
/*     struct core_page* core = find_one_available_page(); */
/*     if (core == NULL) */
/*     { */
/*         return ENOMEM; */
/*     } */
/*     lock_acquire(vm_lock); */
/*  */
/*     entry->corepage_entry = core; */
/*  */
/*     swapin_corepage(entry->corepage_entry, (entry->swappage_entry)); */
/*  */
/*  */
/*     entry->swappage_entry = NULL; */
/*  */
/*     entry->status = PAGETABLE_ENTRY_INRAM; */
/*     struct pagetable_entry* tmp = entry->next; */
/*  */
/*     while (tmp != entry) */
/*     { */
/*         tmp->corepage_entry = entry->corepage_entry; */
/*         tmp->swappage_entry = NULL; */
/*         tmp->status = PAGETABLE_ENTRY_INRAM; */
/*     } */
/*     lock_release(vm_lock); */
/*  */
/*     return 0; */
/* } */
