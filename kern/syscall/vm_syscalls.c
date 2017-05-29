#include <types.h>
#include <vnode.h>
#include <stat.h>
#include <kern/errno.h>
#include <openfile.h>
#include <filetable.h>

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
#include <copyinout.h>
#include <syscall.h>
#define PROT_READ 1
#define PROT_WRITE 2

//in 32 machine, long is always 4 bytes.
int sys_sbrk(intptr_t  amount, int* retval)
{
    *retval = 0;
    if (amount < 0 || (amount & (~PAGE_FRAME)) != 0)
    {
        return EINVAL;
    }
    struct addrspace *as = proc_getas();
    *retval = as_get_heap_break(as, amount);
    if (*retval == -1)
    {
        return ENOMEM;
    }
    return 0;

}
int sys_mmap(size_t length, int prot, int fd, off_t offset, int * retval)
{
    struct openfile *file = NULL;

	int err;

	err = filetable_get(curproc->p_filetable, fd, &file);
	if (err) {
		return err;
	}

    if ((offset & ~(PAGE_FRAME)) != 0 || (length & ~(PAGE_FRAME)) != 0)
    {
        return EINVAL;
    }
    openfile_incref(file);
    struct vnode* vn = get_vnode(file);
    KASSERT(vn != NULL);
    struct addrspace *as = proc_getas();
    void * addr = NULL;
    err = as_define_mmap(as, vn, offset, length >> 12,  PROT_READ & prot, PROT_WRITE & prot,  &addr);
    if (err != 0)
    {
        VOP_DECREF(vn);
        openfile_decref(file);
        return err;
    }
    VOP_DECREF(vn);
    openfile_decref(file);
    *retval = (int) addr;
    return 0;

}

int sys_munmap(void* addr)
{
    if (addr == NULL)
    {
        return EINVAL;
    }
    return as_destroy_mmap(addr);
}

