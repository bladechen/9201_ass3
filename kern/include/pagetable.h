
#ifndef __PAGETABLE_H__
#define __PAGETABLE_H__

#include <types.h>
#include <synch.h>
#include <mips/tlb.h>

#define ASIDMASK  0x00000fc0

#define ENTRYMASK 0xfffff000
#define OFFSETMASK 0x00000fff

#define SWAPMASK    (1<<5)
#define READWRITE   (1<<4)
#define NCACHEMASK  (TLBLO_NOCACHE >> 8)
#define DIRTYMASK   (TLBLO_DIRTY >> 8)
#define VALIDMASK   (TLBLO_VALID >> 8)
#define GLOBALMASK  (1<<0)

#define CONTROLMASK 0x0000000f

#define DEBUGLOAD 1

// Date type for the process ID, is currently an int and can be changed later
typedef int pid_t;

struct hpt_entry;

// This global variable must have get populated with the ramsize in the init function
uint32_t ram_size;

// Main global hashed page table struct
struct hashed_page_table
{
    // Array of PTEs, this should have 2 times the max number of frames in physical memory
    // Or simply use a pointer to hpt_entry such that we can dynamically allocate it
    struct hpt_entry *hpt_entry;
    //struct hpt_entry hpt[NUMBEROFFRAMES]; // Just a representation for now
#ifdef DEBUGLOAD
    // This int holds the number of populated entries in the HPT
    int load;
#endif
    // Spinlock chosen for less overhead compared to struct lock
    // Necessary for concurrency management between processes or even threads in the same process
    struct spinlock *hpt_lock;
};

struct hpt_entry
{
    // Physical frame number with the 12 bit offset
    paddr_t paddr;

    // virtual page number with the 12 bit offset
    vaddr_t vaddr;

    // This is the process ID which can be the address space pointer
    pid_t pid;

    // bits 0 - GLOBAL
    // bits 1 - VALID
    // bits 2 - DIRTY
    // bits 3 - NCACHE
    // bits 4 - READ/WRITE for advanced

    char control;

    // Next pointer for the entries
    struct hpt_entry *next;
};

// this initialises the page table
void init_page_table( void );

// The called can supply the full 32 bits of the VADDR and PADDR to the following FUNCTIONs
// To store an entry into the page table
bool store_entry( vaddr_t vaddr , pid_t pid , paddr_t paddr , char control );

// Remove an entry from the hash table
int remove_page_entry( vaddr_t vaddr, pid_t pid );


// Allocate a page and return the index
// struct hpt_entry * allocate_page( void );

// Is this entry present in the hash table already?
// O(1) to find out
bool is_valid_virtual( vaddr_t vaddr , pid_t pid );

/*
    These 3 functions take and entry and find out the permissions and other meta data
    of the entry
    These functions must only be called if the vaddrs and pids are vaid and exist in pagetable
   */
bool is_valid( vaddr_t vaddr , pid_t pid );
bool is_global( vaddr_t vaddr , pid_t pid );
bool is_dirty( vaddr_t vaddr , pid_t pid );
bool is_non_cacheable( vaddr_t vaddr , pid_t pid );

// TODO
// Set and store in TLB the same way we should reset and store in TLB ?
// mask = GLOBALMASK | DIRTYMASK | VALIDMASK
void set_mask( vaddr_t vaddr , pid_t pid , uint32_t mask);
void reset_mask( vaddr_t vaddr , pid_t pid , uint32_t mask);

// Struct to get the entries for the TLB
// Should return error code if not successfuld
int get_tlb_entry(  vaddr_t vaddr , pid_t pid, uint32_t* tlb_hi, uint32_t* tlb_lo );

// Initialise the hash table and set the fields to the initial values
int init_hashtable( void );

#endif
