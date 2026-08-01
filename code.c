#define _DEFAULT_SOURCE

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

typedef struct block_header block_header_t;

struct block_header {
    size_t size;
    int free;
    block_header_t *prev_free;
    block_header_t *next_free;
};

static block_header_t *free_list_head = NULL;
static block_header_t *heap_start = NULL;

static const size_t ALIGNMENT = 16;
static const size_t MIN_BLOCK_SIZE = sizeof(block_header_t) + sizeof(size_t);

static size_t align_up(size_t size) {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

static size_t block_payload_size(const block_header_t *block) {
    return block->size - sizeof(block_header_t) - sizeof(size_t);
}

static size_t *block_footer(block_header_t *block) {
    return (size_t *)((char *)block + block->size - sizeof(size_t));
}

static block_header_t *next_block(block_header_t *block) {
    return (block_header_t *)((char *)block + block->size);
}

static block_header_t *prev_block(block_header_t *block) {
    size_t prev_size = *(size_t *)((char *)block - sizeof(size_t));
    return (block_header_t *)((char *)block - prev_size);
}

static void write_block(block_header_t *block, size_t size, int free) {
    block->size = size;
    block->free = free;
    *block_footer(block) = size;
}

static void remove_free(block_header_t *block) {
    if (block->prev_free) {
        block->prev_free->next_free = block->next_free;
    } else {
        free_list_head = block->next_free;
    }
    if (block->next_free) {
        block->next_free->prev_free = block->prev_free;
    }
    block->prev_free = NULL;
    block->next_free = NULL;
}

static void insert_free(block_header_t *block) {
    block->free = 1;
    block->prev_free = NULL;
    block->next_free = free_list_head;
    if (free_list_head) {
        free_list_head->prev_free = block;
    }
    free_list_head = block;
}

static block_header_t *coalesce(block_header_t *block) {
    block_header_t *merged = block;

    if ((char *)block > (char *)heap_start + sizeof(size_t)) {
        block_header_t *prev = prev_block(block);
        if (prev->free) {
            remove_free(prev);
            merged = prev;
            write_block(merged, merged->size + block->size, 1);
        }
    }

    block_header_t *next = next_block(merged);
    void *program_break = sbrk(0);
    if ((void *)next < program_break) {
        if (next->free) {
            remove_free(next);
            write_block(merged, merged->size + next->size, 1);
        }
    }

    return merged;
}

static block_header_t *find_fit(size_t total_size) {
    for (block_header_t *block = free_list_head; block; block = block->next_free) {
        if (block->size >= total_size) {
            return block;
        }
    }
    return NULL;
}

static block_header_t *extend_heap(size_t total_size) {
    void *region = sbrk((intptr_t)total_size);
    if (region == (void *)-1) {
        return NULL;
    }

    block_header_t *block = (block_header_t *)region;
    write_block(block, total_size, 1);
    block->prev_free = NULL;
    block->next_free = NULL;

    if (!heap_start) {
        heap_start = block;
    }

    block = coalesce(block);
    insert_free(block);
    return block;
}

static void place(block_header_t *block, size_t total_size) {
    size_t remaining = block->size - total_size;
    remove_free(block);

    if (remaining >= MIN_BLOCK_SIZE) {
        write_block(block, total_size, 0);
        block_header_t *split = (block_header_t *)((char *)block + total_size);
        write_block(split, remaining, 1);
        insert_free(split);
    } else {
        write_block(block, block->size, 0);
    }
}

static int mm_init(void) {
    if (heap_start) {
        return 0;
    }

    heap_start = (block_header_t *)sbrk(0);
    return extend_heap(align_up(4096)) ? 0 : -1;
}

void *malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    if (!heap_start && mm_init() != 0) {
        return NULL;
    }

    size_t payload = align_up(size);
    size_t total_size = payload + sizeof(block_header_t) + sizeof(size_t);
    if (total_size < MIN_BLOCK_SIZE) {
        total_size = MIN_BLOCK_SIZE;
    }

    block_header_t *block = find_fit(total_size);
    if (!block) {
        size_t grow_size = total_size > 4096 ? total_size : 4096;
        block = extend_heap(align_up(grow_size));
        if (!block) {
            return NULL;
        }
    }

    place(block, total_size);
    return (char *)block + sizeof(block_header_t);
}

void free(void *ptr) {
    if (!ptr) {
        return;
    }

    block_header_t *block = (block_header_t *)((char *)ptr - sizeof(block_header_t));
    write_block(block, block->size, 1);
    block = coalesce(block);
    insert_free(block);
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    block_header_t *block = (block_header_t *)((char *)ptr - sizeof(block_header_t));
    size_t old_size = block_payload_size(block);
    void *new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    size_t copy_size = old_size < size ? old_size : size;
    memcpy(new_ptr, ptr, copy_size);
    free(ptr);
    return new_ptr;
}

void *calloc(size_t nmemb, size_t size) {
    if (nmemb && size > SIZE_MAX / nmemb) {
        return NULL;
    }
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

int main(void) {
    return 0;
}
