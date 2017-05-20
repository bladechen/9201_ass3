#include <types.h>
#include <lib.h>
#include <mips/tlb.h>
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>


static void get_all_tlb_slots(uint32_t tlb_array[NUM_TLB][2])
{
	int spl = splhigh();
	for (int i = 0; i < NUM_TLB; i ++)
	{
		tlb_read(&tlb_array[i][0], &tlb_array[i][1], i);
	}
	splx(spl);
	return;
}

// FIXME, only for finding bugs while deleloping, disable it while submit
static bool sanity_check_tlb()
{
	int spl = splhigh();
	uint32_t tlb_array[NUM_TLB][2];
	get_all_tlb_slots(tlb_array);
	for (int i = 0; i < NUM_TLB; i ++)
	{
		for (int j = i + 1; j < NUM_TLB; j ++)
		{
			KASSERT((tlb_array[i][0] & TLBHI_VPAGE) != (tlb_array[j][0] & TLBHI_VPAGE));
		}
	}
	splx(spl);
	return true;

}


void tlb_invalid_by_vaddr(vaddr_t vaddr)
{
	KASSERT((vaddr  & (~ TLBHI_VPAGE))  == 0);
	int spl = splhigh();

	int index = tlb_probe(vaddr, 0);
	if(index>0)
	{
		tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(),index);
	}
	splx(spl);
	return;

}


void tlb_invalid_by_paddr(paddr_t paddr)
{
	KASSERT((paddr  & (~TLBLO_PPAGE) ) == 0);
	int spl = splhigh();
	sanity_check_tlb();
	uint32_t tlb_hi = 0;
	uint32_t tlb_lo = 0;
	for (int i = 0; i < NUM_TLB; i ++)
	{
		tlb_read(&tlb_hi, &tlb_lo, i);
		if ((tlb_lo & TLBLO_PPAGE) == paddr)
		{
			tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
		}

	}
	splx(spl);
	return;
}

void tlb_flush()
{
	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();
	sanity_check_tlb();
	for (int i=0; i<NUM_TLB; i++)
	{
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

