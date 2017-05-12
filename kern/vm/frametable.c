#include <kern/types.h>
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <clock.h>
#include <addrspace.h>
#include <vm.h>

/* Place your frametable data-structures here
 * You probably also want to write a frametable initialisation
 * function and call it from vm_bootstrap
 */

/* static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER; */

// defined in vm.c
/* extern struct core_page* pageframe_table ; */
/* extern int frametable_size ; */
struct core_page* pageframe_table = NULL;
int frametable_size = 0; // max index of pageframe_table

paddr_t firstfree_addr = 0;
static struct spinlock pageframe_lock = SPINLOCK_INITIALIZER;

static volatile int current_frametable_index = 0;
static void as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}


static int paddr_2_frametable_idx(paddr_t paddr)
{
    KASSERT((paddr & (~PAGE_FRAME)) == 0);
    int idx  = ((paddr_t)paddr - firstfree_addr) / PAGE_SIZE;
    KASSERT(idx >= 0 && idx < frametable_size);
    return idx;
}

static int is_kernel_page(struct core_page * page)
{
    KASSERT(page != NULL);
    return (page->corepage_status == COREPAGE_KERNEL
            && page->owner == NULL
            && page-> locked == 0
            && page -> access_ms == 0);
}


/* Note that this function returns a VIRTUAL address, not a physical
 * address
 * WARNING: this function gets called very early, before
 * vm_bootstrap().  You may wish to modify main.c to call your
 * frame table initialisation function, or check to see if the
 * frame table has been initialised and call ram_stealmem() otherwise.
 */
static void free_corepage(struct core_page* page)
{

    page->owner = NULL;
    page->access_ms = 0;
    page->corepage_status = COREPAGE_FREE;

    /* page->locked = 0; */
    /* page->chunk_allocated = 0; */

    // clear when allocated
    as_zero_region(page->p_addr, 1);
    return;
}

static bool is_free_corepage(struct core_page* page)
{
    return (page->owner == NULL &&  page->corepage_status == COREPAGE_FREE &&  page->locked == 0);
}

/**
 * @brief: find the physical mem with size of npages that has not been used
 *
 * the caller should hold the pageframe_lock
 *
 * @param:  npages currently only support npages == 1
 *
 * @return: -1 not find, otherwise index with npages avalable
 */
static struct core_page* find_free_pages(unsigned int npages)
{
    KASSERT(npages == 1);
    KASSERT(spinlock_do_i_hold(&pageframe_lock));

    bool find = false;
    int cur_idx = current_frametable_index;
    while (1)
    {
        if (is_free_corepage(&(pageframe_table[cur_idx])))
        {
            find = true;
            break;
        }
        cur_idx = (cur_idx + 1) % frametable_size;
        if (cur_idx == current_frametable_index)
        {
            find = false;
            break;
        }
    }
    if (find == false)
    {
        return NULL;
    }

    pageframe_table[cur_idx].locked = 1;
    return pageframe_table + cur_idx;
}

static struct core_page* find_one_available_page()
{
    KASSERT(spinlock_do_i_hold(&pageframe_lock));
    struct core_page* ret = find_free_pages(1);
    if (ret != NULL)
    {
        return ret;
    }
    return NULL;



    // TODO for extended
    /* ret = find_swap_able_pages(1); */
    /* if (ret == NULL) */
    /* { */
    /*     spinlock_release(&pageframe_lock); */
    /*     DEBUG(DB_VM, "do not find an available page\n"); */
    /*     return NULL; */
    /* } */
    /*  */
    /* spinlock_release(&pageframe_lock); */
    /* int r = try_swapout(ret); */
    /*  */
    /* if ( r != 0 ) */
    /* { */
    /*     return NULL; */
    /* } */
    /* #<{(| ret->locked = true; // locked this page for further use |)}># */
    /* return ret; */
}

/* static core_page* find_swap_able_pages(unsigned int npages) */
/* { */
/*     KASSERT(spinlock_do_i_hold(&pageframe_lock)); */
/*     bool find = false; */
/*     int ret_idx = -1; */
/*  */
/*     // LRU in swapping out one page! */
/*     if (npages == 1) */
/*     { */
/*         long long cur_ms = get_curtime_ms(); */
/*  */
/*         for (int i = 0; i < frametable_size; i ++) */
/*         { */
/*             if (pageframe_table[i].access_ms <= cur_ms && */
/*                 pageframe_table[i].corepage_status != COREPAGE_KERNEL && pageframe_table[i].locked == 0) */
/*             { */
/*                 cur_ms = pageframe_table[i].access_ms; */
/*                 ret_idx =  i; */
/*             } */
/*  */
/*         } */
/*         if (ret_idx != -1) */
/*         { */
/*             pageframe_table[ret_idx].locked  = 1; */
/*         } */
/*  */
/*         return  (ret_idx == -1) ? NULL: pageframe_table[ret_idx]; */
/*  */
/*     } */
/*  */
/*     for (int i = 0; i < frametable_size - npages + 1; i ++) */
/*     { */
/*  */
/*         if (pageframe_table[i].corepage_status !=  COREPAGE_KERNEL && pageframe_table[i].locked == 0) */
/*         { */
/*             find = true; */
/*             for (int j = i + 1 ;j < i + npages; j ++) */
/*             { */
/*  */
/*                 if (pageframe_table[j].corepage_status !=  COREPAGE_KERNEL && pageframe_table[j].locked == 0) */
/*                 { */
/*                     find = false; */
/*                     break; */
/*                 } */
/*             } */
/*             if ( find == true) */
/*             { */
/*                 ret_idx = i; */
/*             } */
/*         } */
/*     } */
/*  */
/*     for (int i = ret_idx; i < npages + ret_idx; i ++) */
/*     { */
/*  */
/*         pageframe_table[i].locked = 1; */
/*     } */
/*     return  (ret_idx == -1) ? NULL: pageframe_table[ret_idx]; */
/*  */
/* } */





// kernel can set npages >= 1, user prog can only find one page one time.
static struct core_page* find_available_kpages(unsigned int npages)
{
    KASSERT(npages == 1);
    return find_one_available_page();
}


    // because we only support kmalloc maximum 1 page a time
    //
    /* spinlock_acquire(&pageframe_lock); */
    /* struct core_page* free_pages = find_free_pages(npages); */
    /* if (free_pages != NULL) */
    /* { */
    /*  */
    /*     spinlock_release(&pageframe_lock); */
    /*     return free_pages; */
    /* } */
    /*  */
    /* free_pages = find_swap_able_pages(npages); */
    /* if (free_pages == NULL) */
    /* { */
    /*  */
    /*     spinlock_release(&pageframe_lock); */
    /*     return NULL; */
    /* } */
    /*  */
    /* spinlock_release(&pageframe_lock); */
    /*  */
    /* int i = 0; */
    /* struct core_page* tmp = free_pages; */
    /* int r = 0; */
    /* for (i = 0; i < npages; i ++) */
    /* { */
    /*     tmp = free_pages + i; */
    /*     r = try_swapout(tmp); */
    /*     if (r != 0) */
    /*     { */
    /*         for ( int j = 0 ;j < i ; j++) */
    /*         { */
    /*             free_corepage(free_pages + j); */
    /*             // set locked to 0 */
    /*         } */
    /*         return NULL; */
    /*     } */
    /* } */

    /* return free_pages; */

/* static int try_swapout(struct core_page* core) */
/* { */
/*     KASSERT(core->locked == 1); */
/*     if (core->corepage_status == COREPAGE_KERNEL) */
/*     { */
/*         panic("why swap out kernel page?"); */
/*         return -1; */
/*     } */
/*     else if (core->corepage_status == COREPAGE_FREE) */
/*     { */
/*         return 0; */
/*     } */
/*     else if (core->corepage_status == COREPAGE_USER) */
/*     { */
/*         KASSERT(core->owner != NULL); */
/*         KASSERT(core->owner->status != PAGETABLE_ENTRY_INRAM); */
/*         return  do_swapout(core->owner); */
/*  */
/*     } */
/*     else */
/*     { */
/*         return -1; */
/*     } */
/*     return 0; */
/* } */
// called while allocating user mem

// exported
vaddr_t alloc_kpages(unsigned int npages)
{
    if (pageframe_table == NULL)
    {
        paddr_t addr;
        spinlock_acquire(&pageframe_lock);
        addr = ram_stealmem(npages);
        spinlock_release(&pageframe_lock);
        if(addr == 0)
        {
            return (vaddr_t)0;
        }
        DEBUG(DB_VM, "alloc_kpages via ram_stealmem %x\n", addr);

        return PADDR_TO_KVADDR(addr);
    }
    else
    {
        KASSERT(npages == 1);
        spinlock_acquire(&pageframe_lock);
        struct core_page* kpages = find_available_kpages(npages);
        if (kpages == NULL)
        {
            spinlock_release(&pageframe_lock);
            return ENOMEM;
        }

        // only support npages == 1
        for (int i = 0; i < (int)npages; i ++)
        {
            struct core_page* tmp = kpages + i;
            tmp->locked = 0;
            tmp->corepage_status = COREPAGE_KERNEL;
            tmp->owner = NULL;
            tmp->access_ms = 0; // kernel can not be swapped out, so no use for access time
        }
        spinlock_release(&pageframe_lock);

        DEBUG(DB_VM, "alloc_kpages via vm %x\n", kpages->p_addr);
        return PADDR_TO_KVADDR(kpages->p_addr);
    }
}

// exported
void free_kpages(vaddr_t addr)
{
    spinlock_acquire(&pageframe_lock);
    paddr_t paddr = KVADDR_TO_PADDR(addr);

    int frametable_index = paddr_2_frametable_idx(paddr);
    KASSERT(is_kernel_page(pageframe_table + frametable_index));
    free_corepage(pageframe_table + frametable_index);
    spinlock_release(&pageframe_lock);

    /* for (int i = 0; i < frametable_size; i ++) */
    /* { */
    /*     if (paddr == pageframe_table[i].p_addr && */
    /*         pageframe_table[i].status == PAGE_KERNEL) */
    /*     { */
    /*         for (int j = 1; j < pageframe_table[i].chunk_allocated) */
    /*         { */
    /*             if (pageframe_table[i + j].chunk_allocated != -1 || pageframe_table[i + j].status != PAGE_KERNEL) */
    /*             { */
    /*  */
    /*                 spinlock_release(&pageframe_lock); */
    /*                 panic("kernel page chunk_allocated  error while free!"); */
    /*             } */
    /*  */
    /*         } */
    /*         int chunk = pageframe_table[i].chunk_allocated; */
    /*         for (int j = 0; j < chunk; ++ j) */
    /*         { */
    /*             free_corepage(pageframe_table + i + j); */
    /*         } */
    /*         break; */
    /*     } */
    /*  */
    /* } */
    return;
}


// exported
void init_frametable()
{
    KASSERT(pageframe_table == NULL);

    paddr_t hi_addr = ram_getsize();
    paddr_t lo_addr = ram_getfirstfree();

    // for 4k aligned
    frametable_size  = (hi_addr - lo_addr - PAGE_SIZE) / (PAGE_SIZE+sizeof(struct core_page)); // if can not be div, there should be a little mem wasted, but does not matter!
    pageframe_table = (struct core_page*)(PADDR_TO_KVADDR(lo_addr));

    firstfree_addr = lo_addr + frametable_size * (sizeof (struct core_page));
    if ((firstfree_addr & (~PAGE_FRAME)) != 0)
    {
        firstfree_addr += (~(firstfree_addr & (~PAGE_FRAME))) & (~PAGE_FRAME);
        firstfree_addr ++;
    }
    KASSERT((firstfree_addr & (~PAGE_FRAME)) == 0);

    DEBUG(DB_VM, "before init lo_addr: %x, hi_addr: %x, total_pagecount: %d, first available addr: %x\n", lo_addr, hi_addr, frametable_size, firstfree_addr);

    for (int i = 0; i < frametable_size; i ++)
    {
        if (firstfree_addr + i * PAGE_SIZE > hi_addr )
        {
            frametable_size = i + 1;
        }
        pageframe_table[i].p_addr = firstfree_addr + (paddr_t)(i * PAGE_SIZE);
        free_corepage(pageframe_table + i);
    }
    DEBUG(DB_VM, "after init lo_addr: %x, hi_addr: %x, total_pagecount: %d, first available addr: %x\n", lo_addr, hi_addr, frametable_size, firstfree_addr);

    return;
}

/* bool corepage_shared(struct core_page* corepage) */
/* { */
/*     if (corepage->corepage_status == COREPAGE_USER) */
/*     { */
/*         struct pagetable_entry* e = corepage_entry->owner; */
/*         if (e->next != e) */
/*         { */
/*             return true; */
/*         } */
/*  */
/*     } */
/*     return false; */
/* } */
