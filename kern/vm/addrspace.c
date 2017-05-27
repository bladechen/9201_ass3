/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *        The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

#include <elf.h>
#include <list.h>

#define APPLICATION_STACK_SIZE (18*(PAGE_SIZE))
/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */

static int convert_to_pages(size_t memsize);
static struct as_region_metadata* as_create_region(void);
static int build_pagetable_link(pid_t pid, vaddr_t vaddr, size_t filepages, int writeable);
static void loop_through_region(struct addrspace *as);
static void copy_region(struct as_region_metadata *old, struct as_region_metadata *new)
{
    KASSERT(old != NULL && new != NULL);
    new->region_vaddr = old->region_vaddr;
    new->npages = old->npages;
    new->rwxflag = old->rwxflag;
    new->type = old->type;
    new->region_vnode = old->region_vnode;
    new->region_offset = old->region_offset;
    // The new link is created in the as_add_region_to_list function
}
static void as_set_region(struct as_region_metadata *region, vaddr_t vaddr, struct vnode *file_vnode, off_t region_offset, size_t memsize, size_t region_size, char perm)
{
    region->region_vaddr = vaddr;
    region->npages = convert_to_pages(memsize);
    region->rwxflag = perm;
    region->region_vnode = file_vnode;
    region->region_offset = region_offset;
    region->region_size = region_size;

    if ( (perm & PF_R) != 0 && (perm & PF_W) != 0 && (perm & PF_X) == 0 )
    {
        // if RW- then DATA
        region->type = DATA;
    }
    else if ( (perm & PF_R) != 0 && (perm & PF_W) == 0 && (perm & PF_X) != 0 )
    {
        // if R-X then CODE
        region->type = CODE;
    }
    else
    {
        region->type = OTHER;
    }
}
static void as_add_region_to_list(struct addrspace *as,struct as_region_metadata *temp)
{
    // Add region entry into the data structure
    /* if (as->list == NULL) */
    /* { */
    /*     // If first region then init temp to point to itself */
    /*     INIT_LIST_HEAD(&(temp->link)); */
    /*     as->list = temp; */
    /* } */
    /* else */
    {
        // If not the first entry then add to the queue
        list_add_tail( &(temp->link), &(as->list->head) );
    }
}

struct addrspace *
as_create(void)
{
    struct addrspace *as;

    as = kmalloc(sizeof(struct addrspace));
    if (as == NULL) {
        return NULL;
    }
    as->list = kmalloc(sizeof(struct list));
    INIT_LIST_HEAD(&(as->list->head));
    as->mmap_start = MMAP_VADDR_BEGIN;
    return as;
}

static int alloc_and_copy_frame(struct addrspace *newas, struct as_region_metadata *region, pid_t oldpid)
{
    // copy on write, not alloc new frame .
    KASSERT(region != NULL);
    uint32_t tlb_hi,tlb_lo;
    size_t i = 0;
    /* size_t j = 0; */
    for (i=0;i<region->npages;i++)
    {
        vaddr_t vaddr = region->region_vaddr + i*PAGE_SIZE;
        int result = get_tlb_entry(vaddr,oldpid, &tlb_hi, &tlb_lo);
        if ( result != 0)
        {
            // father not allocate page for that vaddr, may be in bss/data . static int a[100000]
            continue;
        }
        paddr_t paddr = tlb_lo & ENTRYMASK;
        inc_frame_ref(paddr);

        // Store new entry in the Page table
        // no matter writable or not, set dirty bit to 0
        bool retval = store_entry( vaddr, (pid_t) newas, paddr, (as_region_control(region) &(~DIRTYMASK) ));
        if( !retval )
        {
            free_upages(paddr);
            /* as_destroy_region(newas, region); */
            DEBUG(DB_VM, "i dont have enough pages\n");
            // TODO if this fails then something has to be done
            return -1;
        }
        // set father also readonly
        reset_mask(vaddr, oldpid, DIRTYMASK);
    }
    return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{

    //DEBUG(DB_VM, "Fork Started on Process 0x%p\n", old);
    struct addrspace *newas;

    newas = as_create();
    if (newas==NULL) {
        return ENOMEM;
    }

    //DEBUG(DB_VM, "New addrspace created which is 0x%p\n",newas);

    struct list_head *old_region_link=NULL;
    list_for_each(old_region_link, &(old->list->head))
    {
        struct as_region_metadata *new_region = as_create_region();
        if (new_region == NULL)
        {
            // Destroy the already alloced space
            DEBUG(DB_VM, "Not enough memory to allocate region in as_copy\n");
            as_destroy(newas);
            return ENOMEM;
        }
        struct as_region_metadata *old_region = list_entry(old_region_link, struct as_region_metadata, link);

        // transfer content of one region to another
        copy_region(old_region, new_region);

        // Allocate a frame and copy data from old frame to new frame
        int result = alloc_and_copy_frame(newas, new_region, (pid_t) old);

        if (result != 0)
        {
            //DEBUG(DB_VM, "Alloc and copy failed in as_copy\n");
            as_destroy(newas);
            return ENOMEM;
        }
        // add the new region to the new address space
        as_add_region_to_list(newas, new_region);
    }

    loop_through_region(newas);
    *ret = newas;
    return 0;
}

void
as_destroy(struct addrspace *as)
{
    if ( as == NULL )
        return;

    struct list_head *current = NULL;
    struct list_head *tmp_head = NULL;

    list_for_each_safe(current, tmp_head, &(as->list->head))
    {
        struct as_region_metadata* tmp = list_entry(current, struct as_region_metadata, link);
        /* list_del(current); */
        as_flush_region(as, tmp);
        as_destroy_region(as, tmp);
        kfree(tmp);
    }
    // when we get here there should be only one node left in the list
    // So free that node and then free the as struct
    /* as_destroy_region(as->list); */
    kfree(as->list);
    kfree(as);
}

void
as_activate(void)
{
    struct addrspace *as;

    tlb_flush();
    as = proc_getas();
    if (as == NULL) {
        /*
         * Kernel thread without an address space; leave the
         * prior address space in place.
         */
        return;
    }

    /*
     * Write this.
     */
}

void
as_deactivate(void)
{
    as_activate();
    /*
     * Write this. For many designs it won't need to actually do
     * anything. See proc.c for an explanation of why it (might)
     * be needed.
     */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, struct vnode *file_vnode, off_t region_offset,
        size_t memsize, size_t filesize,
        int readable, int writeable, int executable)
{
    /*
     * Write this.
     */
    struct as_region_metadata *temp;
    temp = as_create_region();
    if (temp == NULL)
    {
        return ENOMEM;
    }
	/* Align the region. First, the base... */
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

    as_set_region(temp, vaddr, file_vnode, region_offset, memsize, filesize,
                  readable | writeable | executable
                 );
    as_add_region_to_list(as,temp);

    // Now make the Page table mapping for the filesize bytes only
    size_t filepages = convert_to_pages(filesize);

    // Build page table link
    int retval = build_pagetable_link((pid_t)as, vaddr, filepages, writeable);

    if ( retval != 0 )
    {
        // Destroy address space
        as_destroy(as);
        return ENOMEM;
    }
    return 0;
}

int
as_prepare_load(struct addrspace *as)
{
    /*
     * Write this.
     */

    as->is_loading = 1;
    return 0;
}

static void loop_through_region(struct addrspace *as)
{
    struct list_head *current = NULL;
    struct list_head *tmp_head = NULL;

    list_for_each_safe(current, tmp_head, &(as->list->head))
    {
        struct as_region_metadata* tmp = list_entry(current, struct as_region_metadata, link);
        DEBUG(DB_VM, " region : 0x%x, page: %d\n", tmp->region_vaddr, tmp->npages);
    }
}
int
as_complete_load(struct addrspace *as)
{

    as->is_loading = 0;



    as_deactivate();
    // let us define heap here, because all the segments have been loaded.
    return as_define_heap(as);
}

int as_define_heap(struct addrspace* as)
{
    KASSERT(as != NULL);
    vaddr_t suppose_heap_start = HEAP_VADDR_BEGIN;
    /* vaddr_t suppose_heap_end = 0x60000000; */
    /* size_t page_num = 65536; */


    struct list_head *current = NULL;
    struct list_head *tmp_head = NULL;

    list_for_each_safe(current, tmp_head, &(as->list->head))
    {
        struct as_region_metadata* tmp = list_entry(current, struct as_region_metadata, link);
        DEBUG(DB_VM, " region : 0x%x, page: %d\n", tmp->region_vaddr, tmp->npages);
        // never corrupt with stack.
        if (tmp->type == STACK)
        {
            continue;
        }
        else if(upper_addr(tmp->region_vaddr, tmp->npages) >= suppose_heap_start )
        {
            suppose_heap_start = upper_addr(tmp->region_vaddr, tmp->npages);
        }
    }
    int ret = as_define_region(as, suppose_heap_start, NULL, 0, 0, 0, PF_R, PF_W, 0);
    if ( ret != 0 )
    {
        return ret;
    }
    //kprintf("fuck stack");
    struct list_head *temp = NULL;
    struct as_region_metadata *last_region = NULL;
    list_for_each_prev(temp, &(as->list->head))
    {
        last_region = list_entry(temp, struct as_region_metadata, link);
        last_region->type = HEAP;
        break;
    }

    loop_through_region(as);
    return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
    /*
     * Write this.
     */

    /* Initial user-level stack pointer */
    *stackptr = USERSTACK;

    // Define the stack as a region with 16K size allocated for it
    //   (this must be > 64K so argument blocks of size ARG_MAX will fit) */
    int retval = as_define_region(as, *stackptr - APPLICATION_STACK_SIZE,
            NULL, 0,
            APPLICATION_STACK_SIZE, APPLICATION_STACK_SIZE,
            PF_R, PF_W, 0);

    if ( retval != 0 )
    {
        return retval;
    }
    //kprintf("fuck stack");
    struct list_head *temp = NULL;
    struct as_region_metadata *last_region = NULL;
    list_for_each_prev(temp, &(as->list->head))
    {
        last_region = list_entry(temp, struct as_region_metadata, link);
        last_region->type = STACK;
        break;
    }
    return 0;
}

static int convert_to_pages(size_t memsize)
{
    int pgsize = 0;
    // If non zero memsize then divide by PAGE_SIZE and add 1
    if ( memsize != 0 )
        pgsize = 1 + ((memsize-1)>>12);

    return pgsize;
}

static struct as_region_metadata* as_create_region(void)
{
    struct as_region_metadata *temp = kmalloc(sizeof(*temp));
    return temp;
}

static void as_destroy_part_of_region(struct addrspace *as, struct as_region_metadata * region, int begin, int npages)
{
    KASSERT(as != NULL &&  region != NULL);
    vaddr_t start = region->region_vaddr;
    vaddr_t end = region->region_vaddr  + (region->npages << 12);
    uint32_t tlb_hi, tlb_lo;
    /* size_t i = 0; */
    for (int i=0; i < npages; i++)
    {
        vaddr_t vaddr_del = begin + i*PAGE_SIZE;
        if (vaddr_del>= start && vaddr_del < end)
        {
        // free page table entry
            int res = get_tlb_entry(vaddr_del,(pid_t) as, &tlb_hi, &tlb_lo);
            if ( res != 0 )
            {
                continue;
            }
            tlb_lo = tlb_lo & ENTRYMASK;
            // free the frame
            free_upages(tlb_lo);
            // Delete PTE related to this
            // TODO the error case for this !!!
            // i don't think we should handle this error, kassert it only.
            KASSERT(0 == remove_page_entry(vaddr_del, (pid_t)as));
        }
    }


}

void as_destroy_region(struct addrspace *as, struct as_region_metadata *to_del)
{
    KASSERT(as != NULL && to_del != NULL);
    list_del(&(to_del->link));
    as_destroy_part_of_region(as, to_del, to_del->region_vaddr, to_del->npages);
}
static void as_flush_region(struct addrspace *as, struct as_region_metadata* region)
{
    KASSERT(region != NULL && as != NULL);
    if ((region->rwxflag & PF_W) && (region->region_vnode != NULL) && region->type == MMAP)
    {
        uint32_t tlb_hi, tlb_lo ;
        for (int i=0; i<region->npages;i++)
        {
            vaddr_t vaddr = region->region_vaddr + i*PAGE_SIZE;
            int result = get_tlb_entry(vaddr, (pid_t)as, &tlb_hi, &tlb_lo);
            if ( result != 0)
            {
                continue;
            }
            paddr_t paddr = tlb_lo & ENTRYMASK;

			void* kbuf = (void*)PADDR_TO_KVADDR(paddr);

            /* Only lock the seek position if we're really using it. */
            off_t file_pos = region->region_offset + i*PAGE_SIZE;

			struct iovec iov;
			struct uio kuio;
			/* set up a uio with the buffer, its size, and the current offset */
            uio_kinit(&iov, &kuio, kbuf, PAGE_FRAME, file_pos, UIO_WRITE);

			int ret = VOP_WRITE(region->region_vnode, &kuio);
            if (ret != 0)
            {
                kprintf("what happen with VOP_WRITE in flush region: %d\n", ret);
                return ;

            }

        }

    }
    return;
}

int as_destroy_mmap(void* addr)
{
    struct addrspace* as = proc_getas();
    KASSERT(as != NULL);
    struct as_region_metadata *region = get_region(as, (vaddr_t)addr);
    if (region == NULL)
    {
        return EINVAL;
    }
    as_flush_region(as, region);
    as_destroy_region(as, region);
    kfree(region);
    return 0;

}

int as_define_mmap(struct addrspace* as, struct vnode* vn, off_t base_offset, int npages , int writable, int readable, void** addr)
{
    KASSERT(as != NULL);
    KASSERT(vn != NULL);
    KASSERT((as->mmap_start & (~PAGE_FRAME)) == 0);
    *addr = NULL;
    /* as->mmap_start += (npages << 12); */
/* as_define_region(struct addrspace *as, vaddr_t vaddr, struct vnode *file_vnode, off_t region_offset, */
/*         size_t memsize, size_t filesize, */
/*         int readable, int writeable, int executable) */

    int ret = as_define_region(as, as->mmap_start, vn, base_offset, npages << 12, npages << 12, readable ? PF_R:0,
                     writeable?PR_W:0, 0);
    if (ret != 0)
    {
        return ret;
    }
    struct list_head *temp = NULL;
    struct as_region_metadata *last_region = NULL;
    list_for_each_prev(temp, &(as->list->head))
    {
        last_region = list_entry(temp, struct as_region_metadata, link);
        last_region->type = MMAP;
        break;
    }


    *addr = as->mmap_start;
    as->mmap_start += npages << 12;
    return 0;
}

int as_get_heap_break(struct addrspace* as, intptr_t amount)
{
    KASSERT(amount >= 0 && (amount & (~PAGE_FRAME)) == 0);
    KASSERT(as != NULL);
    struct list_head *current = NULL;
    struct list_head *tmp_head = NULL;
    struct as_region_metadata* tmp = NULL;

    list_for_each_safe(current, tmp_head, &(as->list->head))
    {
        tmp = list_entry(current, struct as_region_metadata, link);
        if (tmp->type == HEAP)
        {
            break;
        }
    }

    KASSERT(tmp != NULL);
    if (amount == 0)
    {
        return (tmp->region_vaddr + (tmp->npages << 12));
    }
    else
    {
        int inc_pages = amount >> 12;
        int heap_break = (tmp->region_vaddr + (tmp->npages << 12));
        int ret = build_pagetable_link((pid_t)as, heap_break, inc_pages, PF_W);
        if (ret != 0)
        {
            goto alloc_heap_fail;
        }

        tmp->npages += (amount>>12);
        return heap_break;
alloc_heap_fail:
        as_destroy_part_of_region(as, tmp, heap_break, inc_pages);
        return -1;
    }

}
char as_region_control(struct as_region_metadata* region)
{
    KASSERT(region != NULL);
    char control = 0;
    if (region->rwxflag & PF_W )
    {
        control |= DIRTYMASK;
    }
    control |= VALIDMASK;
    return control;

}

static int build_pagetable_link(pid_t pid, vaddr_t vaddr, size_t filepages, int writeable)
{
    vaddr_t page_vaddr = 0;
    //pid_t pid = (pid_t) as;

    size_t i = 0;
    for (i=0;i<filepages;i++)
    {
        paddr_t paddr = get_free_frame();
        if ( paddr == 0 )
        {
            return ENOMEM;
        }

        // Construct the control bits for the PTE
        // Just set validmask for now, all entries are cacheable and none are global
        char control = VALIDMASK;
        page_vaddr = vaddr + i*PAGE_SIZE;

        if ( (writeable&PF_W) != 0 )
        {
            control |= DIRTYMASK;
        }
        else
        {
            control &= (~DIRTYMASK);
        }
        bool result = store_entry(page_vaddr, pid, paddr, control);
        if (!result)
        {
            // because the last page was allocated but would never get freed as the
            // PTE for this dosent exist
            free_upages(paddr);
            return ENOMEM;
        }
    }
    return 0;
}

