
#include <pagetable.h>
#include <hashlib.h>
#include <vm.h>
#include <lib.h>

#define ENOPTE 4
#define HASHLENGTH 8
// Global structs to define

static struct hashed_page_table *hpt = NULL;

// This is the free list struct to be used in external chaining
static struct hashed_page_table *free_entries = NULL;

// Pointer to the head of the free list
static struct hpt_entry *free_head = NULL;

// hashtable_size should be initialised in the init function
static int hashtable_size = 0;
static const void *emptypointer = NULL;

// Prototypes defined to avoid compiler error
static void construct_key( vaddr_t vaddr, pid_t pid , unsigned char* ptr );
static struct hpt_entry* get_free_entry( void );
static bool is_equal(vaddr_t vaddr ,pid_t pid , struct hpt_entry* current );

/*  Hash algorithm to calculate the value pair for the given key
    Note the hash's key is the virtual page address and the process id (which is what it acts on)
    This function should return an integer index into the array of the hash table entries
*/
static int hash( vaddr_t vaddr , pid_t pid )
{
    (void) pid;
    KASSERT(vaddr != 0);
    unsigned char key[HASHLENGTH];
    construct_key(vaddr, pid, key);
    
    int index = calculate_hash(key, HASHLENGTH, hashtable_size);

    return index;
    // return 1;
}

// this initialises the page table
void init_page_table( void )
{
    // no need to lock here
    ram_size = ram_getsize();
    KASSERT( ram_size > 0 );

    DEBUG(DB_VM, "RAM SIZE is %d\n", ram_size);
    // allocate the memory for the hashed_page_table 
    hpt = (struct hashed_page_table *) kmalloc(sizeof(*hpt));
    KASSERT(hpt != NULL);

    // allocate the memory for the free entries for external chaining
    free_entries = (struct hashed_page_table *) kmalloc(sizeof(*free_entries));
    KASSERT(free_entries != NULL);

    DEBUG(DB_VM, "Hash Page Table Initialised...\n");
    int number_of_frames = ram_size/PAGE_SIZE;

    // The size of hashtable is only equal to the number of frames but
    // freelist has the same number of entries as well
    hashtable_size = number_of_frames;

    // Allocate for the hpt_entries array, kmalloc will call ram_stealmem if the vm bootstrap hasnt been complete
    hpt->hpt_entry = kmalloc(hashtable_size * sizeof(struct hpt_entry));
    KASSERT(hpt->hpt_entry != NULL);

    // This is the free list chain which should be initilised to chain to next entry
    free_entries->hpt_entry = kmalloc(hashtable_size * sizeof(struct hpt_entry));
    KASSERT(free_entries->hpt_entry != NULL);

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

        free_entries->hpt_entry[i].vaddr = (vaddr_t) emptypointer;
        free_entries->hpt_entry[i].paddr = (paddr_t) emptypointer;

        // Chain the freelist to the next entry one after the other
        if( i < (hashtable_size - 1) )
            free_entries->hpt_entry[i].next = &(free_entries->hpt_entry[i+1]);
        else
            free_entries->hpt_entry[i].next = (struct hpt_entry *) emptypointer;
    }

    // The free_head points to the first entry when initialised
    free_head = &(free_entries->hpt_entry[0]);

    // Initialise locks
    spinlock_init(hpt->hpt_lock);
    spinlock_init(free_entries->hpt_lock);

#ifdef DEBUGLOAD
    // set load to zero
    hpt->load = 0;
    // set free list count to be maximum
    free_entries->load = number_of_frames;
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
static bool is_colliding( vaddr_t vaddr, pid_t pid )
{
    int index = hash(vaddr,pid);
    spinlock_acquire(hpt->hpt_lock);
    if ( hpt->hpt_entry[index].next == NULL )
    {
        spinlock_release(hpt->hpt_lock);
        return false;
    }
    spinlock_release(hpt->hpt_lock);
    return true;
}

// WARNING no lock for this function, caller must have lock between this function
static void store_in_table( vaddr_t vaddr, pid_t pid, paddr_t paddr, struct hpt_entry* hpt_ent )
{
    hpt_ent->vaddr = vaddr;
    hpt_ent->paddr = paddr;
    hpt_ent->pid = pid;
    hpt_ent->next = NULL;
}

static void set_page_zero( struct hpt_entry* current )
{
    store_in_table( (vaddr_t) emptypointer,(pid_t) emptypointer,(paddr_t) emptypointer, current);
}

// TODO locks here
// To store an entry into the page table
void store_entry( vaddr_t vaddr , pid_t pid, paddr_t paddr )
{
    int index = hash(vaddr,pid);
    KASSERT ( index > -1 );

    spinlock_acquire(hpt->hpt_lock);
    if ( !is_colliding( vaddr , pid ) )
        store_in_table(vaddr, pid, paddr, &(hpt->hpt_entry[index]) );
    else
    {
        // index pointer
        struct hpt_entry *current = &(hpt->hpt_entry[index]);
        // The chained pointer
        struct hpt_entry *nextchained = hpt->hpt_entry[index].next;

        // Free entry
        // Get free entry from free list
        // NESTED LOCK, TODO need to check if this can cause deadlock !!!
        spinlock_acquire(free_entries->hpt_lock);
        struct hpt_entry *free = get_free_entry();
        spinlock_release(free_entries->hpt_lock);

        // TODO When there are no more free nodes
        if ( free == NULL )
        {
            spinlock_release(hpt->hpt_lock);
            return;
        }
        // Store in table
        store_in_table( vaddr, pid, paddr, free );
        // link in the next chain
        current->next = free;
        // What if free is NULL
        free->next = nextchained;
    }
    spinlock_release(hpt->hpt_lock);
}

/*
   this is the FREE LIST MANAGEMENT SECTION
*/
static void add_to_freelist( struct hpt_entry* to_add )
{
    KASSERT( to_add != NULL );
    spinlock_acquire(free_entries->hpt_lock);
    // Chain to the head of the list
    to_add->next = free_head;
    // Update the free list pointer
    free_head = to_add;
    spinlock_release(free_entries->hpt_lock);
}

// Gets an entry from the freelist
static struct hpt_entry* get_free_entry( void )
{
    struct hpt_entry *temp;
    spinlock_acquire(free_entries->hpt_lock);
    if ( free_head == NULL )
    {
        temp = NULL;
    }
    else
    {
        // pop the first element and move freelist head to next element
        temp = free_head;
        free_head = free_head->next;
    }
    spinlock_release(free_entries->hpt_lock);
    return temp;
}

// Remove an entry from the hash table
void remove_page_entry( vaddr_t vaddr, pid_t pid )
{
    KASSERT(vaddr != 0);
    // Get hash index
    int index = hash(vaddr, pid);
    struct hpt_entry *current = &(hpt->hpt_entry[index]);
    struct hpt_entry *prev;
    spinlock_acquire(hpt->hpt_lock);

    // Check if the index matches the vaddr and pid
    if ( is_equal(vaddr,pid,current) )
    {
        set_page_zero(current);
        spinlock_release(hpt->hpt_lock);
        return;
    }
    else
    {
        prev = current;
        current = current->next;
        while( current != NULL )
        {
            // check if the vaddr and pid are the same
            // if they are then release that node to the free pool
            if( is_equal(vaddr,pid,current) )
            {
                // Redirect the pointers
                prev->next = current->next;

                // set to zeros
                // TODO this is not really necessary
                // If its in the free pool the data dosent matter
                set_page_zero(current); 

                // Release node into free pool
                add_to_freelist(current);

                spinlock_release(hpt->hpt_lock);
                return;
            }
            prev = current;
            current = current->next;
        }
        spinlock_release(hpt->hpt_lock);
        return;
    }
}

// TODO needs to be REVIEWED
// Gets the physical frame address in memory
struct hpt_entry* get_page( vaddr_t vaddr , pid_t pid )
{
    KASSERT(vaddr != 0);
    // Get hash index
    int index = hash(vaddr, pid);
    struct hpt_entry* current = &(hpt->hpt_entry[index]);
    spinlock_acquire(hpt->hpt_lock);

    // Check if the index matches the vaddr and pid
    if ( is_equal(vaddr,pid,current) )
    {
        spinlock_release(hpt->hpt_lock);
        return current;
    }
    else
    {
        // Still have the lock
        current = current->next;
        while( current != NULL )
        {
            // check if the vaddr and pid are the same
            // if they are then return current pointer
            if( is_equal(vaddr,pid,current) )
            {
                spinlock_release(hpt->hpt_lock);
                return current;
            }
            current = current->next;
        }
    }
    // if we get here then current should be NULL
    spinlock_release(hpt->hpt_lock);
    return current;
}

// TODO
// Allocate a page and return the index 
struct hpt_entry* allocate_page( void )
{
    return NULL;
}

// WARNING this dosent have a lock the caller should have a lock around this!!!
static bool is_equal(vaddr_t vaddr ,pid_t pid , struct hpt_entry* current )
{
   if ( (vaddr == current->vaddr) && (pid == current->pid) )
       return true;

   return false;
}

// Is this entry present in the hash table already?
// O(1) to find out
bool is_valid_virtual( vaddr_t vaddr , pid_t pid )
{
    KASSERT(vaddr != 0);

    int index = hash(vaddr, pid);
    spinlock_acquire(hpt->hpt_lock);
    if ( hpt->hpt_entry[index].vaddr != 0 )
    {
        if ( hpt->hpt_entry[index].pid == pid )
        {
            spinlock_release(hpt->hpt_lock);
            return true;
        }
        else
        {
            struct hpt_entry* current = hpt->hpt_entry[index].next;
            while( current != NULL )
            {
                // check if the vaddr and pid are the same
                // if they are then return true
                if( is_equal(vaddr,pid,current) )
                {
                    spinlock_release(hpt->hpt_lock);
                    return true;
                }
            }
        }
    }
    spinlock_release(hpt->hpt_lock);
    return false;
}

/* 
    These 4 functions take and entry and find out the permissions and other meta data
    of the entry
   */

bool is_valid( const struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    spinlock_acquire(hpt->hpt_lock);
    if ( (pte->paddr & GLOBALMASK) == VALIDMASK )
    {
        spinlock_release(hpt->hpt_lock);
        return true;
    }
    spinlock_release(hpt->hpt_lock);
    return false;
}

bool is_global( const struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    spinlock_acquire(hpt->hpt_lock);
    if ( (pte->paddr & GLOBALMASK) == GLOBALMASK )
    {
        spinlock_release(hpt->hpt_lock);
        return true;
    }
    spinlock_release(hpt->hpt_lock);
    return false;
}

bool is_dirty( const  struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    spinlock_acquire(hpt->hpt_lock);
    if ( (pte->paddr & DIRTYMASK) == DIRTYMASK )
    {
        spinlock_release(hpt->hpt_lock);
        return true;
    }
    spinlock_release(hpt->hpt_lock);
    return false;
}

bool is_non_cacheable( const struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    spinlock_acquire(hpt->hpt_lock);
    if ( (pte->paddr & NCACHEMASK) == NCACHEMASK )
    {
        spinlock_release(hpt->hpt_lock);
        return true;
    }
    spinlock_release(hpt->hpt_lock);
    return false;
}

void set_valid( struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    spinlock_acquire(hpt->hpt_lock);
    pte->paddr |= VALIDMASK;
    spinlock_release(hpt->hpt_lock);
}

void set_global( struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    pte->paddr |= GLOBALMASK;
    spinlock_release(hpt->hpt_lock);
}

void set_dirty( struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    spinlock_acquire(hpt->hpt_lock);
    pte->paddr |= DIRTYMASK;
    spinlock_release(hpt->hpt_lock);
}

void set_noncachable( struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    spinlock_acquire(hpt->hpt_lock);
    pte->paddr |= NCACHEMASK;
    spinlock_release(hpt->hpt_lock);
}

// Struct to get the entries for the TLB
// Should return error code if not successful
int get_tlb_entry( struct hpt_entry* pte, int* tlb_hi, int* tlb_lo )
{
    (void) tlb_hi;
    (void) tlb_lo;

    KASSERT(pte!=NULL);

    int index = hash(pte->vaddr, pte->pid);
    (void) index;
    return -1;
}

