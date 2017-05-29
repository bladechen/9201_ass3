#include <types.h>
#include <spl.h>
#include <elf.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <proc.h>
#include <addrspace.h>
#include <mips/tlb.h>
#include <vm.h>
#include <machine/tlb.h>
#include <pagetable.h>



struct lock *vm_lock = NULL;
extern struct frame_entry* free_entry_list;

void vm_bootstrap(void)
{


    /* init_coreswap(); */
    DEBUG(DB_VM, "init_frametable ing....\n");
    init_page_table();
    test_pagetable();
    init_frametable();

    DEBUG(DB_VM, "init_frametable 0x%p finish\n", free_entry_list);
    return;

}


int vm_fault(int faulttype, vaddr_t faultaddress)
{
	uint32_t tlb_hi, tlb_lo;
	struct addrspace *as;

	faultaddress &= PAGE_FRAME;

    if (curproc == NULL)
    {
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL)
    {
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
        if (!(region ->rwxflag & PF_W))
        {
            DEBUG(DB_VM, "not writable 0x%x\n", faultaddress);
            return EFAULT;
        }
        // this region is writable
        else // copy on write, readonly occur on writable region
        {
            KASSERT(0 == get_tlb_entry(faultaddress, pid, &tlb_hi, &tlb_lo));
            paddr_t old_frame = tlb_lo&PAGE_FRAME;
            paddr_t frame = dup_frame(old_frame);
            if (frame == 0)
            {
                DEBUG(DB_VM, "copy on write, i think i am running out of mem!\n");
                return ENOMEM;
            }
            else if (frame == old_frame)
            {
                set_mask(faultaddress, pid, DIRTYMASK);
                tlb_flush();
            }
            else
            {
                update_page_entry(faultaddress, pid, frame, as_region_control(region));
                tlb_flush();

            }
            /* return 0; */
        }
    }

    int ret = get_tlb_entry(faultaddress, pid, &tlb_hi, &tlb_lo);
    if (ret == 0)
    {
        KASSERT(check_user_frame(tlb_lo & PAGE_FRAME));
        tlb_force_write(tlb_hi, tlb_lo);
    }
    else
    {
        ret = load_frame(region, faultaddress);
        if (ret != 0)
        {
            return ret;
        }
        ret = get_tlb_entry(faultaddress, pid, &tlb_hi, &tlb_lo);
        if (ret != 0)
        {
           panic("what happen in get_tlb_entry");
        }
        tlb_force_write(tlb_hi, tlb_lo);

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

