
#include <pagetable.h>
#include <vm.h>
#include <lib.h>

#define ENOPTE 4
// Global structs to define

static struct hashed_page_table *hpt = NULL;

// This is the free list struct to be used in external chaining
static struct hpt_entry *freelist = NULL;
// Pointer to the head of the free list
static struct hpt_entry *free_head = NULL;

static int hashtable_size = 0;
static const void *emptypointer = NULL;

static void construct_key( vaddr_t vaddr, pid_t pid , unsigned char* ptr );
/*  Hash algorithm to calculate the value pair for the given key
    Note the hash's key is the virtual page address (which is what it acts on)
    This function should return an integer index into the array of the hash table entries

    I think this should be static as its internal the the page table...
*/
static int hash( vaddr_t vaddr , pid_t pid )
{
    (void) pid;
    if ( vaddr == 0 )
        return -1;
    char key[8];
    construct_key(vaddr, pid, key);

    return 1;
}

// this initialises the page table
void init_page_table( void )
{
    // no need to lock here
    ram_size = ram_getsize();
    KASSERT( ram_size > 0 );

    // allocate the memory for the hashed_page_table 
    hpt = (struct hashed_page_table *) kmalloc(sizeof(*hpt));

    int number_of_frames = ram_size/PAGE_SIZE;

    // The size of hashtable is only equal to the number of frames but
    // freelist has the same number of entries as well
    hashtable_size = number_of_frames;

    // Allocate for the hpt_entries array, kmalloc will call ram_stealmem if the vm bootstrap hasnt been complete
    hpt->hpt_entry = kmalloc(hashtable_size * sizeof(struct hpt_entry));

    // This is the free list chain which should be initilised to chain to the other
    freelist = kmalloc(hashtable_size * sizeof(struct hpt_entry));

    // set all values hpt_entries (vaddr and paddr) to point to global free pointer
    int i = 0;
    for (i = 0; i<hashtable_size; i++)
    {
        // Virtual addrs are nulls
        hpt->hpt_entry[i].vaddr = (vaddr_t) emptypointer;
        // Frames are nulls
        hpt->hpt_entry[i].paddr = (paddr_t) emptypointer;
        // Next points to null
        hpt->hpt_entry[i].next = (struct hpt_entry *) emptypointer;

        freelist[i].vaddr = (vaddr_t) emptypointer;
        freelist[i].paddr = (paddr_t) emptypointer;

        // Chain the freelist to the next entry one after the other
        if( i != (hashtable_size - 1) )
            freelist[i].next = &(freelist[i+1]);
        else
            freelist[i].next = NULL;
    }

    // The free_head points to the first entry when initialised
    free_head = &(freelist[0]);

#ifdef DEBUGLOAD
    // set load to zero
    hpt->load = 0;
#endif
}

// Helper function to construct the key for the hash function
static void construct_key( vaddr_t vaddr, pid_t pid , unsigned char* ptr )
{
    int i;
    for(i=0;i<8;i++)
    {
        if(i<4)
            ptr[i] = ( vaddr >> i*8 ) & 0xff;
        else
            ptr[i] = ( pid >> (i-4)*8 ) & 0xff;
    }
}
// See if there are collisions with the hash index
static bool is_colliding( vaddr_t vaddr , pid_t pid )
{
    int index = hash(vaddr,pid);
    if ( hpt->hpt_entry[index].next == NULL )
        return false;
    return true;
}

static void store_in_table ( vaddr_t vaddr, pid_t pid, paddr_t paddr, int index )
{
    hpt->hpt_entry[index].vaddr = vaddr;
    hpt->hpt_entry[index].paddr = paddr;
    hpt->hpt_entry[index].pid = pid;
    hpt->hpt_entry[index].next = NULL;
}

// To store an entry into the page table
void store_entry( paddr_t paddr, vaddr_t vaddr , pid_t pid )
{
    int index = hash(vaddr,pid);
    KASSERT ( index > -1 );

    if ( !is_colliding( vaddr , pid ) )
    {
        store_in_table (vaddr, pid, paddr, index );
    }
    else
    {
        struct hpt_entry *current = hpt->hpt_entry[index].next;
        (void) current;
    }
}


/*
   this is the FREE LIST MANAGEMENT SECTION
static void add_to_freelist( struct hpt_entry* to_add )
{
    KASSERT( to_add != NULL );
    // Chain to the head of the list
    to_add->next = free_head;
    // Update the free list pointer
    free_head = to_add;
}

// Gets an entry from the freelist
static struct hpt_entry* get_free_entry( void )
{
    if ( free_head == NULL );
    // Error case TODO
    else
    {
        // pop the first element and move freelist head to next element
        struct hpt_entry *temp;
        temp = free_head;
        free_head = free_head->next;
        return temp;
    }
}

*/

// TODO
// Remove an entry from the hash table
void remove_page_entry( vaddr_t vaddr, pid_t pid );

// TODO
// Gets the physical frame address in memory
paddr_t get_frame( vaddr_t vaddr , pid_t pid );

// TODO
// Allocate a page and return the index 
struct hpt_entry* allocate_page( int page_num );

// Is this entry present in the hash table already?
// O(1) to find out
bool is_valid_virtual( vaddr_t vaddr , pid_t pid , int *retval )
{
    if ( vaddr == 0 )
    {
        *retval = ENOPTE;
        return false;
    }
    int index = hash(vaddr, pid );
    (void) index;
    (void) pid;
    return false;
}

/* 
    These 4 functions take and entry and find out the permissions and other meta data
    of the entry
   */

bool is_valid( const struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    if ( (pte->paddr & GLOBALMASK) == VALIDMASK )
        return true;
    return false;
}

bool is_global( const struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    if ( (pte->paddr & GLOBALMASK) == GLOBALMASK )
        return true;
    return false;
}

bool is_dirty( const  struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    if ( (pte->paddr & DIRTYMASK) == DIRTYMASK )
        return true;
    return false;
}
bool is_non_cacheable( const struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    if ( (pte->paddr & NCACHEMASK) == NCACHEMASK )
        return true;
    return false;
}

void set_valid( struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    pte->paddr |= VALIDMASK;
}

void set_global( struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    pte->paddr |= GLOBALMASK;
}

void set_dirty( struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    pte->paddr |= DIRTYMASK;
}

void set_noncachable( struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    pte->paddr |= NCACHEMASK;
}

// Struct to get the entries for the TLB
// Should return error code if not successful
int get_tlb_entry( struct hpt_entry* pte, int* tlb_hi, int* tlb_lo )
{
    (void) tlb_hi;
    (void) tlb_lo;

    if ( pte == NULL )
        return -1;
    int index = hash(pte->vaddr, pte->pid);
    (void) index;
    return -1;
}

