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

#define APPLICATION_STACK_SIZE 18*PAGE_SIZE
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
static void copy_region(struct as_region_metadata *old, struct as_region_metadata *new)
{
    new->region_vaddr = old->region_vaddr;
    new->npages = old->npages;
    new->rwxflag = old->rwxflag;
    new->type = old->type;
    new->region_vaddr = old->region_vaddr;

    // The new link is created in the as_add_to_list function
}
static void as_set_region(struct as_region_metadata *region, vaddr_t vaddr, size_t memsize, char perm)
{
    region->region_vaddr = vaddr;
    region->npages = convert_to_pages(memsize);
    region->rwxflag = perm;

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
static void as_add_to_list(struct addrspace *as,struct as_region_metadata *temp)
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
    return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
    struct addrspace *newas;

    newas = as_create();
    if (newas==NULL) {
        return ENOMEM;
    }

    struct list_head *old_region_link=NULL;
    list_for_each(old_region_link, &(old->list->head))
    {
        struct as_region_metadata *new_region = as_create_region();
        if (new_region == NULL)
        {
            return ENOMEM;
        }
        struct as_region_metadata *old_region = list_entry(old_region_link, struct as_region_metadata, link);
        // loop through all the regions
        copy_region(old_region, new_region);
        // add the new region to the new address space
        as_add_to_list(*ret, new_region);
    }

    *ret = newas;
    return 0;
}

void
as_destroy(struct addrspace *as)
{
    struct list_head *current = NULL;
    struct list_head *tmp_head = NULL;

/* @pos:    the pointer of struct list_head* to use as a loop counter.
 * @n:      tm pointer of struct list_head*
 * @l:      the list pointer
 */
/* #define list_for_each_entry_safe(pos, n, l)\ */
     /* for ( pos = ((l)->head.next == &((l)->head) ? NULL:list_get_entry_from_link((l)->head.next)), \ */

    // TODO check this again
    list_for_each_safe(current, tmp_head, &(as->list->head))
    {
        struct as_region_metadata* tmp = list_entry(current, struct as_region_metadata, link);
        list_del(current);
        as_destroy_region(tmp);
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
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize, size_t filesize,
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

    as_set_region(temp, vaddr, memsize,
                  readable | writeable | executable
                 );
    as_add_to_list(as,temp);

    // Now make the Page table mapping for the filesize bytes only
    size_t filepages = convert_to_pages(filesize);
    size_t i = 0;
    vaddr_t page_vaddr = 0;
    pid_t pid = (pid_t) as;

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

        if ( writeable != 0 )
        {
            control |= DIRTYMASK;
        }
        else
        {
            control &= (~DIRTYMASK);
        }
        bool result = store_entry (page_vaddr, pid, paddr, control);

        if (!result)
        {
            return ENOMEM;
        }
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

int
as_complete_load(struct addrspace *as)
{
    /*
     * Write this.
     */

    struct list_head *current = NULL;
    struct list_head *tmp_head = NULL;

/* @pos:    the pointer of struct list_head* to use as a loop counter.
 * @n:      tm pointer of struct list_head*
 * @l:      the list pointer
 */
/* #define list_for_each_entry_safe(pos, n, l)\ */
     /* for ( pos = ((l)->head.next == &((l)->head) ? NULL:list_get_entry_from_link((l)->head.next)), \ */

    // TODO check this again
    list_for_each_safe(current, tmp_head, &(as->list->head))
    {
        struct as_region_metadata* tmp = list_entry(current, struct as_region_metadata, link);
        DEBUG(DB_VM, " region : 0x%x, page: %d\n", tmp->region_vaddr, tmp->npages);
    }
    // when we get here there should be only one node left in the list
    // So free that node and then free the as struct
    /* as_destroy_region(as->list); */

    as->is_loading = 0;

    as_deactivate();
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
    int retval = as_define_region(as, *stackptr - APPLICATION_STACK_SIZE, APPLICATION_STACK_SIZE, APPLICATION_STACK_SIZE, PF_R, PF_W, 0);

    if ( retval != 0 )
    {
        return retval;
    }
    kprintf("fuck stack");
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

void as_destroy_region(struct as_region_metadata *to_del)
{
    // currently nothing in as_region_metadata is kmalloced so just kfree the datastructure
    kfree(to_del);
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
