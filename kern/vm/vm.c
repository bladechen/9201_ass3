#include <types.h>
#include <elf.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <proc.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <pagetable.h>
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
    init_page_table();
    test_pagetable();
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

inline static vaddr_t upper_addr(vaddr_t addr, int pages)
{
    KASSERT(pages >= 1);
    return (addr + (pages << 12));

}
static struct as_region_metadata* get_region(struct addrspace* space, vaddr_t faultaddress)
{
    KASSERT(space != NULL);
    KASSERT(space->list != NULL);
    KASSERT(!(faultaddress & OFFSETMASK));
    struct as_region_metadata* cur = NULL;
    struct list_head* head = &(space->list->head);
    list_for_each_entry(cur, head, link)
    {
        if (cur->region_vaddr <= faultaddress && upper_addr(cur->region_vaddr, cur->npages) > faultaddress)
        {
            return cur;
        }
    }
    return NULL;

}

int vm_fault(int faulttype, vaddr_t faultaddress)
{
	uint32_t tlb_hi, tlb_lo;
	struct addrspace *as;

	faultaddress &= PAGE_FRAME;

    if (curproc == NULL)
    {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL)
    {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}
    if (faultaddress == 0) // NULL pointer
    {
        return EFAULT;
    }
	/* DEBUG(DB_VM, "fault: 0x%x, type: %d\n", faultaddress, faulttype); */
    pid_t pid = (pid_t) as;

    struct as_region_metadata* region = get_region(as, faultaddress);
    if (region == NULL)
    {
        DEBUG(DB_VM, "Couldnt find region 0x%x\n", faultaddress);
        return EFAULT;
    }
    if ( (faulttype == VM_FAULT_READONLY) )
    {
        KASSERT(!(region->rwxflag & PF_W));
        DEBUG(DB_VM, "not writable 0x%x\n", faultaddress);
        return EFAULT;
    }
    /* if (!is_valid_virtual(faultaddress, pid)) */
    /* { */
    /*     DEBUG(DB_VM, "not in page table 0x%x\n", faultaddress); */
    /*     return EFAULT; */
    /* } */

    // TODO KASSERT frame page is user frame
    int ret = get_tlb_entry(faultaddress, pid, &tlb_hi, &tlb_lo);
    /* if (is_valid_virtual(faultaddress, pid)) */
    if (ret == 0)
    {

        KASSERT(check_user_frame(tlb_lo & PAGE_FRAME));
        int write_permission = (as->is_loading == 1) ? TLBLO_DIRTY:0;

        tlb_lo |= write_permission;
        tlb_random(tlb_hi, tlb_lo);
    }
    else
    {
        paddr_t frame_addr = get_free_frame();
        if (frame_addr == 0)
        {
            return ENOMEM;
        }
        char ctrl = as_region_control(region);

        bool result = store_entry (faultaddress, pid, frame_addr, ctrl);

        if (!result)
        {
            free_upages(frame_addr);
            return ENOMEM;
        }
        ret = get_tlb_entry(faultaddress, pid, &tlb_hi, &tlb_lo);
        if (ret != 0)
        {
            panic("what happen in get_tlb_entry");
        }
        KASSERT(check_user_frame(tlb_lo & PAGE_FRAME));
        /* KASSERT(as->is_loading == 0); */
        int write_permission = (as->is_loading == 1) ? TLBLO_DIRTY:0;

        tlb_lo |= write_permission;
        tlb_random(tlb_hi, tlb_lo);

    }
    return 0;
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

