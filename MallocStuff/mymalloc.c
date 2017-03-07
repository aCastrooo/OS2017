#include <stdbool.h>
#include "mymalloc.h"

#define MAX_MEMORY 5000


/**
 * The Block struct will be used to keep track of memory blocks.
 * They form a linked list with rootBlock as the head and NULL as a tail.
 * Each block requires 16 bytes of memory.
 */
typedef struct Block {
    bool isFree;
    unsigned short size; // We use short instead of size_t to save space
    struct Block* next;
} Block;


static const unsigned short BLOCK_SIZE = sizeof(Block);
static char myblock[MAX_MEMORY];
static bool firstMalloc = true;
static Block* rootBlock;


/**
 * Initialize the root block. This is only called the first time that mymalloc is used.
 */
static void initializeRoot() {
    rootBlock = (Block*) myblock;
    rootBlock->isFree = true;
    rootBlock->size = MAX_MEMORY - BLOCK_SIZE;
    rootBlock->next = NULL;
    firstMalloc = false;
}


/**
 * Performs a basic check to ensure that the pointer address is contained
 * inside myblock. It does not verify that all of the promised space is in myblock,
 * nor does it verify that the address is actually correct (e.g. a Block pointer).
 *
 * @param ptr Check if this pointer points to an address in myblock.
 * @return true if ptr is in myblock, false otherwise.
 */
static bool inMemorySpace(Block* ptr) {
    return (char*) ptr >= &myblock[0] && (char*) ptr <= &myblock[MAX_MEMORY - 1];
}


/**
 * Free the block and merge it with its neighbors if they are free as well. Iterates
 * over the entire list of blocks until the correct block is found, while keeping
 * track of the previous block. When the correct block is found, it is marked as free.
 * If the previous and/or next blocks are free, they will be merged into one.
 *
 * @param toFree the Block to be freed.
 * @return true if toFree was freed, false otherwise.
 */
static bool freeAndMerge(Block* toFree) {

    Block* previous = NULL;
    Block* current = rootBlock;

    // Iterate through the list until toFree is found.
    do {

        if (current == toFree) {

            current->isFree = true;
            Block* next = current->next;

            // Merge current with next if next is free.
            if (next != NULL && next->isFree) {
                current->size += next->size + BLOCK_SIZE;
                current->next = next->next;
            }

            // Merge previous with current if previous is free.
            if (previous != NULL && previous->isFree) {
                previous->size += current->size + BLOCK_SIZE;
                previous->next = current->next;
            }

            return true;
        }

        else {
            previous = current;
        }

    } while ((current = current->next) != NULL);

    // Return false if toFree was not found.
    return false;
}


/**
 * Allocates memory in a Block, if possible, and returns a pointer to the position of the block
 * that contains the memory. The first 16 bits of the memory chunk are used to store the
 * information of the Block. The Blocks are chained together in a linked list, and new blocks
 * are added with a first-fit placement strategy.
 *
 * @param file defined to be __FILE__, used to print messages when an error is detected.
 * @param line defined to be __LINE__, used to print messages when an error is detected.
 * @return void pointer to memory in myblock
 */
void* mymalloc(size_t size, const char* file, int line) {

    // If it is the first time this function has been called, then initialize the root block.
    if (firstMalloc) {
        initializeRoot();
    }

    Block* current = rootBlock;
    const unsigned short sizeWithBlock = size + BLOCK_SIZE; // Include extra space for metadata.

    // First fit placement strategy.
    do {

        // Look for free block with enough space.
        if (!current->isFree || current->size < size) {
            continue;
        }

        else if (current->isFree) {

            if (current->size == size) {
                // Mark current block as taken and return it.
                current->isFree = false;
                return ((char*) current) + BLOCK_SIZE;
            }

            // If a free block has more than enough space, create new free block to take up the rest of the space.
            else if (current->size >= sizeWithBlock) {

                Block* newBlock = (Block*) ((char*) current + sizeWithBlock);

                newBlock->isFree = true;
                newBlock->size = current->size - sizeWithBlock;
                newBlock->next = current->next;

                current->next = newBlock;
                current->size = size;

                // Mark current block as taken and return it.
                current->isFree = false;
                return ((char*) current) + BLOCK_SIZE;
            }

            /* NOTE: If current->size is greater than size, but less than sizeWithBlock,
             * then there is not enough room to accommodate both the space and a new Block,
             * so we continue the search. */
        }

    } while ((current = current->next) != NULL);

    // If no suitable free block is found, print an error message and return NULL pointer.
    printf("Error at line %d of %s: not enough space is available to allocate.\n", line, file);
    return NULL;
}


/**
 * Checks if the block is eligible to be freed, and frees it if it is.
 */
void myfree(void* ptr, const char* file, int line) {

    if (!inMemorySpace(ptr)) {
        printf("Error at line %d of %s: pointer was not created using malloc.\n", line, file);
    }

    else {

        Block* block = (Block*) ((char*) ptr - BLOCK_SIZE);

        if (block->isFree) {
            printf("Error at line %d of %s: pointer has already been freed.\n", line, file);
        }

        else if (!freeAndMerge(block)) {
            printf("Error at line %d of %s: pointer was not created using malloc.\n", line, file);
        }
    }
}
