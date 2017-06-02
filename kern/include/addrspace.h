/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
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

#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

/*
 * Address space structure and operations.
 */


#include <vm.h>
#include <list.h>
#include "opt-dumbvm.h"

struct vnode;

/*
 * Address space - data structure associated with the virtual memory
 * space of a process.
 *
 * You write this.
 */
#define HEAP_VADDR_BEGIN 0x50000000
#define MMAP_VADDR_BEGIN 0x60000000
#define HEAP_VADDR_END (MMAP_VADDR_BEGIN - 1)
#define MMAP_VADDR_END (0x6fffffff)
enum region_type {
    CODE,
    DATA,
    STACK,
    HEAP,
    MMAP,
    OTHER
};

struct as_region_metadata {
    vaddr_t region_vaddr;
    size_t npages;

    // bit 0 is X  PF_X
    // bit 1 is W  PF_W
    // bit 2 is R  PF_R
    char rwxflag;

    enum region_type type;

    // Advanced part for demand loading
    struct vnode *vn;

    // File offset of the vnode
    vaddr_t vnode_vaddr;
    off_t vnode_offset;

    // region size
    size_t vnode_size;

    // Link to the next data struct
    struct list_head link;
};

struct addrspace {
#if OPT_DUMBVM
    vaddr_t as_vbase1;
    paddr_t as_pbase1;
    size_t as_npages1;
    vaddr_t as_vbase2;
    paddr_t as_pbase2;
    size_t as_npages2;
    paddr_t as_stackpbase;
#else
    /* Put stuff here for your VM system */
    // Linked list of as_region_metadatas
    // struct as_region_metadata *list;
    struct list *list;
    char is_loading;

    vaddr_t mmap_start;
#endif
};

/*
 * Functions in addrspace.c:
 *
 *    as_create - create a new empty address space. You need to make
 *                sure this gets called in all the right places. You
 *                may find you want to change the argument list. May
 *                return NULL on out-of-memory error.
 *
 *    as_copy   - create a new address space that is an exact copy of
 *                an old one. Probably calls as_create to get a new
 *                empty address space and fill it in, but that's up to
 *                you.
 *
 *    as_activate - make curproc's address space the one currently
 *                "seen" by the processor.
 *
 *    as_deactivate - unload curproc's address space so it isn't
 *                currently "seen" by the processor. This is used to
 *                avoid potentially "seeing" it while it's being
 *                destroyed.
 *
 *    as_destroy - dispose of an address space. You may need to change
 *                the way this works if implementing user-level threads.
 *
 *    as_define_region - set up a region of memory within the address
 *                space.
 *
 *    as_prepare_load - this is called before actually loading from an
 *                executable into the address space.
 *
 *    as_complete_load - this is called when loading from an executable
 *                is complete.
 *
 *    as_define_stack - set up the stack region in the address space.
 *                (Normally called *after* as_complete_load().) Hands
 *                back the initial stack pointer for the new process.
 *
 * Note that when using dumbvm, addrspace.c is not used and these
 * functions are found in dumbvm.c.
 */

struct addrspace *as_create(void);
int               as_copy(struct addrspace *src, struct addrspace **ret);
void              as_activate(void);
void              as_deactivate(void);
void              as_destroy(struct addrspace *);

int               as_define_region(struct addrspace *as,
                                   vaddr_t vaddr, struct vnode *file_vnode, off_t region_offset,
                                   size_t memsz, size_t filesz,
                                   int readable,
                                   int writeable,
                                   int executable, enum region_type);
int               as_prepare_load(struct addrspace *as);
int               as_complete_load(struct addrspace *as);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);

int as_destroy_mmap(void* addr);
int as_define_mmap(struct addrspace* as, struct vnode* vn, off_t base_offset, int npages ,int readable,  int writable, void** addr);
int as_define_heap(struct addrspace* as);
int as_get_heap_break(struct addrspace* as, intptr_t amount);

// Function for dynamic loading of a page into memory
int load_frame(struct as_region_metadata *, vaddr_t faultaddress);

// Additions
void as_destroy_region(struct addrspace *as, struct as_region_metadata *to_del);
/*
 * Functions in loadelf.c
 *    load_elf - load an ELF user program executable into the current
 *               address space. Returns the entry point (initial PC)
 *               in the space pointed to by ENTRYPOINT.
 */

char as_region_control(struct as_region_metadata* region);
int load_elf(struct vnode *v, vaddr_t *entrypoint);
int force_get_tlb(vaddr_t vaddr, pid_t pid, uint32_t* tlb_hi, uint32_t* tlb_lo);



static inline vaddr_t upper_addr(vaddr_t addr, int pages);
static inline struct as_region_metadata* get_region(struct addrspace* space, vaddr_t faultaddress)
{
    KASSERT(space != NULL);
    KASSERT(space->list != NULL);
    KASSERT(!(faultaddress & OFFSETMASK));
    struct as_region_metadata* cur = NULL;
    struct list_head* head = &(space->list->head);
    list_for_each_entry(cur, head, link)
    {
        if (cur->region_vaddr <= faultaddress && (vaddr_t)upper_addr(cur->region_vaddr, cur->npages) > faultaddress)
        {
            return cur;
        }
    }
    return NULL;

}


#endif /* _ADDRSPACE_H_ */
