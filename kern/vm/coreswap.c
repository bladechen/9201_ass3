#include <types.h>
#include <vfs.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <coreswap.h>
#include <vnode.h>
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <kern/iovec.h>
#include <uio.h>
#include <stat.h>


#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <coreswap.h>

#include <elf.h>
#include <uio.h>
#include <list.h>
#include <vnode.h>
#include <pagetable.h>
/* #include <mips/vm.h> */

static struct vnode *coreswap_file = NULL;
static struct swap_page* coreswap_map = NULL;

static int swapmap_size = 0;


static struct lock* swap_lock = NULL;
/* static struct spinlock swap_lock = SPINLOCK_INITIALIZER; */



static struct swap_page* free_swap_list = NULL;


void init_coreswap_file()
{
    int ret = vfs_open((char*)"/os161_coreswap", O_RDWR | O_CREAT , 0, &coreswap_file);
    if (ret != 0)
    {
        panic("vfs open failed, ret: %d\n", ret);
    }
    swap_lock = lock_create("");
    KASSERT(swap_lock != NULL);

}
void init_coreswap()
{

    swapmap_size = SWAP_SIZE_TIMES_PHYSICAL * ram_getsize()/PAGE_SIZE ; // swap area set to be two times the physical mem
    coreswap_map = (struct swap_page*) kmalloc(swapmap_size * sizeof(struct swap_page));
    if (coreswap_map == NULL)
    {
        panic("kmalloc swap map failed\n");
    }

    for (int i = 0; i < swapmap_size ; i ++)
    {
        coreswap_map[i].owner =  NULL;
        coreswap_map[i].offset = i * PAGE_SIZE;
        coreswap_map[i].next_free = free_swap_list;
        free_swap_list = &(coreswap_map[i]);
    }
    return;
}

void destroy_coreswap(void)
{
    if (coreswap_file != NULL)
    {
        vfs_close(coreswap_file);
        coreswap_file = NULL;
    }
    /* if (coreswap_map != NULL) */
    /* { */
    /*     kfree(coreswap_map); */
    /*     coreswap_map = NULL; */
    /* } */
    lock_destroy(swap_lock);
    return;
}

/**
 * @brief:
 *
 * @param:  paddr physical mem to swap out
 * @param:  offset , swap area
 */
static void swapout(paddr_t paddr, off_t offset)
{
    KASSERT(paddr % PAGE_SIZE == 0);
    KASSERT(offset % PAGE_SIZE == 0);
    struct iovec iov;
    struct uio swap_uio;
    uio_kinit(&iov,
              &swap_uio,
              (void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE,
              offset,
              UIO_WRITE);
    DEBUG(DB_VM, "swap out physic mem: %x to swap file at [%llu]\n", paddr, offset);
    int ret = VOP_WRITE(coreswap_file, &swap_uio);
    if (ret != 0)
    {
        panic("core at swap out vop_writing\n");
    }

    return;
}

static void swapin(paddr_t paddr, int offset)
{
    KASSERT(paddr % PAGE_SIZE == 0);
    KASSERT(offset % PAGE_SIZE == 0);
    struct iovec iov;
    struct uio swap_uio;
    uio_kinit(&iov,
              &swap_uio,
              (void*)PADDR_TO_KVADDR(paddr), PAGE_SIZE,
              offset,
              UIO_READ);
    DEBUG(DB_VM, "swap in physic mem: %x from swap file at [%d]\n", paddr, offset);
    int ret = VOP_READ(coreswap_file, &swap_uio);
    if (ret != 0)
    {
        panic("core at swap in vop_read\n");
    }
    return;
}

static struct swap_page* alloc_swap_page()
{
    lock_acquire(swap_lock);
    if (free_swap_list == NULL)
    {
        lock_release(swap_lock);
        return NULL;
    }
    struct swap_page* ret = free_swap_list;
    free_swap_list = free_swap_list->next_free;
    ret->next_free = NULL;
    lock_release(swap_lock);
    return ret;
}
static void free_swap_page(struct swap_page* page)
{

    KASSERT(page != NULL);
    /* spinlock_acquire(&swap_lock); */

    page->owner = NULL;
    page->next_free = free_swap_list;

    free_swap_list = page;
    /* spinlock_release(&swap_lock); */
    return;
}


int swapout_frame(struct frame_entry* frame)
{
    KASSERT(frame != NULL);
    struct swap_page* swap = alloc_swap_page();
    if (swap == NULL)
    {
        return -1;
    }
    swapout(frame->p_addr, swap->offset);
    /* kprintf("swapout: paddr: %x, owner: %p\n", frame->p_addr, frame->owner); */
    swap->owner = frame->owner;
    KASSERT(frame->reference_count == 1);
    {
        set_page_swapout(frame->owner, swap->offset);
    }


    return 0;
}

int swapin_frame(struct frame_entry* frame, int swap_offset)
{
    KASSERT(frame != NULL);
    /* KASSERT(frame->frame_status == FREE_FRAME); */
    KASSERT(swap_offset >= 0 && swap_offset < swapmap_size * PAGE_SIZE);
    KASSERT(swap_offset % PAGE_SIZE == 0);

    lock_acquire(swap_lock);
    struct swap_page* swap = &(coreswap_map[swap_offset/PAGE_SIZE]);
    swapin( frame->p_addr, swap_offset);

    frame->reference_count = 1;
    frame->owner = swap->owner;
    KASSERT(frame->reference_count == 1);
    {
        set_page_swapin(frame->owner, frame->p_addr);
    }

    free_swap_page(swap);
    lock_release(swap_lock);

    return 0;
}

void free_swap(int swap_offset)
{
    KASSERT(swap_offset >= 0 && swap_offset < swapmap_size * PAGE_SIZE);
    KASSERT(swap_offset % PAGE_SIZE == 0);
    lock_acquire(swap_lock);
    struct swap_page* swap = &(coreswap_map[swap_offset/PAGE_SIZE]);
    KASSERT(swap->owner != NULL);
    free_swap_page(swap);
    lock_release(swap_lock);

}
