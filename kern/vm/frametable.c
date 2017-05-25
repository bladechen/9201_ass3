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
/* extern struct frame_entry* frame_table ; */
/* extern int frametable_size ; */
struct frame_entry* frame_table = NULL;
int free_list_count;
struct frame_entry* free_entry_list = NULL;
int frametable_size = 0; // max index of frame_table

paddr_t firstfree_addr = 0;
static struct spinlock frame_lock = SPINLOCK_INITIALIZER;

static struct spinlock free_frame_list_lock = SPINLOCK_INITIALIZER;

static void as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}


static int paddr_2_frametable_idx(paddr_t paddr)
{
    KASSERT((paddr & (~PAGE_FRAME)) == 0);
    int idx  = (int)(paddr >> 12);
    KASSERT(idx >= 0 && idx < frametable_size);
    return idx;
}
static bool is_user_frame(struct frame_entry * frame)
{
    KASSERT(frame != NULL);
    return (frame ->frame_status == USER_FRAME
            && frame->owner == NULL
            && frame->next_free == NULL
            &&frame-> locked == 0);
}

static bool is_kernel_frame(struct frame_entry * frame)
{
    KASSERT(frame != NULL);
    return (frame ->frame_status == KERNEL_FRAME
            && frame->owner == NULL
            && frame->next_free == NULL
            &&frame-> locked == 0);
}

static void clear_frame(struct frame_entry* frame, int frame_status)
{
    KASSERT(spinlock_do_i_hold(&frame_lock));
    KASSERT(frame != NULL);
    frame->owner = NULL;
    frame->frame_status = frame_status;
    frame->locked = 0;
    frame->pinned = 0;
    frame->next_free = NULL;
    as_zero_region(frame->p_addr, 1);
    return ;

}

static void free_frame_entry(struct frame_entry* entry)
{

    KASSERT(entry != NULL);
    spinlock_acquire(&free_frame_list_lock);
    KASSERT(entry->next_free == NULL);
    entry->owner = NULL;
    entry->frame_status = FREE_FRAME;
    entry->locked  = 0;
    entry->next_free = free_entry_list;
    free_entry_list = entry;

    free_list_count++;
    spinlock_release(&free_frame_list_lock);
    return;
}

/* static bool is_free_frame_entry(struct frame_entry* entry) */
/* { */
/*     return (entry->owner == NULL && entry->frame_status == FREE_FRAME && entry->locked == 0); */
/* } */

/**
 * @brief: find the physical mem with size of npages that has not been used
 *
 * the caller should hold the pageframe_lock
 *
 * @param:  npages currently only support npages == 1
 *
 * @return: -1 not find, otherwise index with npages avalable
 */
static struct frame_entry* find_free_frame(unsigned int npages)
{
    KASSERT(npages == 1);
    spinlock_acquire(&free_frame_list_lock);
    if (free_entry_list == NULL)
    {
        spinlock_release(& free_frame_list_lock);
        return NULL;

    }
    struct frame_entry* e = free_entry_list;

    free_entry_list = e->next_free;
    free_list_count--;
    spinlock_release(&free_frame_list_lock);
    return e;
}

static struct frame_entry* find_one_available_frame()
{
    struct frame_entry* ret = find_free_frame(1);

    if (ret != NULL)
    {
        return ret;
    }
    return NULL;
}

// exported
vaddr_t alloc_kpages(unsigned int npages)
{
    if (frame_table == NULL)
    {
        paddr_t addr;
        spinlock_acquire(&frame_lock);
        addr = ram_stealmem(npages);
        spinlock_release(&frame_lock);
        if(addr == 0)
        {
            return (vaddr_t)0;
        }
        DEBUG(DB_VM, "alloc_kpages via ram_stealmem Number of Pages:%2d base address: %x\n", npages, addr);

        return PADDR_TO_KVADDR(addr);
    }
    else
    {
        KASSERT(npages == 1);
        spinlock_acquire(&frame_lock);
        struct frame_entry*  tmp = find_one_available_frame();
        if (tmp == NULL)
        {
            spinlock_release(&frame_lock);
            return 0;
        }

        clear_frame(tmp, KERNEL_FRAME);
        spinlock_release(&frame_lock);

        /* DEBUG(DB_VM, "alloc_kpages via vm %x\n", tmp->p_addr); */
        return PADDR_TO_KVADDR(tmp->p_addr);
    }
}

static vaddr_t alloc_upages()
{
    spinlock_acquire(&frame_lock);
    struct frame_entry*  tmp = find_one_available_frame();
    if (tmp == NULL)
    {
        spinlock_release(&frame_lock);
        return 0;
    }
    clear_frame(tmp, USER_FRAME);
    spinlock_release(&frame_lock);

    /* DEBUG(DB_VM, "alloc_kpages via vm %x\n", tmp->p_addr); */
    return PADDR_TO_KVADDR(tmp->p_addr);
}

// Returns a free frame from the frame table
paddr_t get_free_frame(void)
{

    vaddr_t addr = alloc_upages();
    if (addr == 0)
    {
        return 0;
    }
    return KVADDR_TO_PADDR(addr);
}

void free_upages(paddr_t paddr)
{
    int frametable_index = paddr_2_frametable_idx(paddr);
    /* DEBUG(DB_VM, "free: %x\n", paddr); */
    spinlock_acquire(&frame_lock);
    KASSERT(is_user_frame(frame_table + frametable_index));
    spinlock_release(&frame_lock);

    free_frame_entry(frame_table + frametable_index);
    return;


}
// exported
void free_kpages(vaddr_t addr)
{
    paddr_t paddr = KVADDR_TO_PADDR(addr);

    int frametable_index = paddr_2_frametable_idx(paddr);
    //DEBUG(DB_VM, "free: %x\n", paddr);
    spinlock_acquire(&frame_lock);
    KASSERT(is_kernel_frame(frame_table + frametable_index));
    spinlock_release(&frame_lock);

    free_frame_entry(frame_table + frametable_index);
    return;
}

// exported
void init_frametable()
{
    KASSERT(frame_table == NULL);

    paddr_t hi_addr = ram_getsize();
    paddr_t lo_addr = ram_getfirstfree();

    // for 4k aligned
    frametable_size = (hi_addr)/(PAGE_SIZE);
    frame_table = (struct frame_entry*)(PADDR_TO_KVADDR(lo_addr));

    firstfree_addr = lo_addr + frametable_size * (sizeof (struct frame_entry));
    if ((firstfree_addr & (~PAGE_FRAME)) != 0)
    {
        firstfree_addr += (~(firstfree_addr & (~PAGE_FRAME))) & (~PAGE_FRAME);
        firstfree_addr ++;
    }
    KASSERT((firstfree_addr & (~PAGE_FRAME)) == 0);

    DEBUG(DB_VM, "before init lo_addr: %x, hi_addr: %x, first available addr: %x\n", lo_addr, hi_addr, firstfree_addr);

    free_list_count = 0;

    for (int i = frametable_size  - 1; i >= 0; i --)
    {
        frame_table[i].p_addr =  (paddr_t)(i * PAGE_SIZE);
        if (frame_table[i].p_addr >= firstfree_addr)
        {
            free_frame_entry(frame_table + i);
        }
        else
        {
            struct frame_entry* frame = &(frame_table[i]);
            frame->owner = NULL;
            frame->frame_status = KERNEL_FRAME;
            frame->locked = 0;
            frame->pinned = 0;
            frame->next_free = NULL;
        }
    }
    frame_table[0].frame_status = NULL_FRAME;
    DEBUG(DB_VM, "after init lo_addr: 0x%x, hi_addr: 0x%x, total_pagecount: %d, first available addr: 0x%x\n", lo_addr, hi_addr, frametable_size, firstfree_addr);

    DEBUG(DB_VM, "\nTotal Frames in memory: %d\nNumber of free frames: %d\n", frametable_size, free_list_count);

    unsigned long size_inbytes_frametable = frametable_size * sizeof(struct frame_entry);
    DEBUG(DB_VM, "Size of Frame table: %2lu\n", size_inbytes_frametable );
    return;
}


bool check_user_frame(paddr_t paddr)
{
    int frametable_index = paddr_2_frametable_idx(paddr);
    /* DEBUG(DB_VM, "free: %x\n", paddr); */
    bool ret = 0;

    spinlock_acquire(&frame_lock);
    ret = is_user_frame(frame_table + frametable_index);
    spinlock_release(&frame_lock);
    return ret;

}
