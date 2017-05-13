
#ifndef __PAGETABLE_H__
#define __PAGETABLE_H__

#include <types.h>
#include <synch.h>

#define ASIDMASK  0x00000fc0
#define SWAPMASK  0x00000001

#define ENTRYMASK 0xfffff000
#define OFFSETMASK 0x00000fff

#define NCACHEMASK  (1<<11)
#define DIRTYMASK   (1<<10)
#define VALIDMASK   (1<<9)
#define GLOBALMASK  (1<<8)

// Date type for the process ID, is currently an int and can be changed later
typedef int pid_t;

struct hpt_entry;

// This global variable must have get populated with the ramsize in the init function
int ram_size;

// Main global hashed page table struct
struct hashed_page_table
{
    // Array of PTEs, this should have 2 times the max number of frames in physical memory
    // Or simply use a pointer to hpt_entry such that we can dynamically allocate it
    struct hpt_entry *hpt_entry;
    //struct hpt_entry hpt[NUMBEROFFRAMES]; // Just a representation for now

    // This int holds the number of populated entries in the HPT
    int load;

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

    // The chain_index should provide the index of the array if chain exists
    // otherwise should be -1
    int next;
};

// this initialises the page table
void init_page_table( void );

// To store an entry into the page table
void store_entry( paddr_t paddr, vaddr_t vaddr , pid_t pid );

// Remove an entry from the hash table
void remove_page_entry( vaddr_t vaddr, pid_t pid );

// Gets the physical frame address in memory
paddr_t get_frame( vaddr_t vaddr , pid_t pid );

// Allocate a page and return the index 
struct hpt_entry * allocate_page( int page_num );

// Is this entry present in the hash table already?
// O(1) to find out
bool is_valid_virtual( vaddr_t vaddr , pid_t pid , int *retval );

/* 
    These 3 functions take and entry and find out the permissions and other meta data
    of the entry
   */
bool is_global( const struct hpt_entry* pte , int *retval );
bool is_dirty( const  struct hpt_entry* pte , int *retval );
bool is_non_cacheable( const struct hpt_entry* pte , int *retval );

// Struct to get the entries for the TLB
// Should return error code if not successfuld
int get_tlb_entry( struct hpt_entry* pte, int* tlb_hi, int* tlb_lo );

// Initialise the hash table and set the fields to the initial values
int init_hashtable( void );

#endif
