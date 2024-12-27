#include <stdio.h>
#include <stdint.h>
#include <string.h>

// 定义内存大小：30MB（这里为了测试可以设置小一点）
#define MEMORY_SIZE (1024 * 1024*30)

// 对齐单位（以8字节对齐）
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

// 全局内存池
static char _HeapMemory_[MEMORY_SIZE];

// 内存块头结构
typedef struct Block {
    size_t size;             // 当前块的大小（不含头部）
    int is_free;             // 是否空闲：1-空闲，0-已分配
    struct Block* next;      // 指向下一个块
    struct Block* prev;      // 指向前一个块（用于双链表优化）
} Block;

// 定义块头大小
#define BLOCK_SIZE (ALIGN(sizeof(Block)))

// 全局空闲链表头
static Block* free_list = NULL;
static int MemInitialized = 0;

// 定义最小分割块大小
#define MIN_BLOCK_SIZE (ALIGN(sizeof(Block) + 16))

// 初始化内存管理
static inline void MEM_init() {
    free_list = (Block*)_HeapMemory_;
    free_list->size = MEMORY_SIZE - BLOCK_SIZE;
    free_list->is_free = 1;
    free_list->next = NULL;
    free_list->prev = NULL;
    MemInitialized = 1;
}

// 延迟合并相邻空闲块
static void delayed_merge() {
    Block* current = free_list;
    while (current) {
        if (current->is_free && current->next && current->next->is_free) {
            current->size += BLOCK_SIZE + current->next->size;
            current->next = current->next->next;
            if (current->next) {
                current->next->prev = current;
            }
        } else {
            current = current->next;
        }
    }
}

// 分配内存
static inline void* _malloc(size_t size) {
    if (!MemInitialized) MEM_init();

    // 对齐请求的大小
    size = ALIGN(size);

    Block* current = free_list;
    Block* best_fit = NULL;

    // 遍历空闲链表，寻找最佳适配块
    while (current) {
        if (current->is_free && current->size >= size) {
            if (!best_fit || current->size < best_fit->size) {
                best_fit = current;
            }
        }
        current = current->next;
    }

    // 如果没有找到合适的块，尝试延迟合并
    if (!best_fit) {
        delayed_merge(); // 合并空闲块
        current = free_list;

        // 再次尝试分配
        while (current) {
            if (current->is_free && current->size >= size) {
                best_fit = current;
                break;
            }
            current = current->next;
        }
    }

    // 如果仍然找不到合适的块，返回 NULL
    if (!best_fit) return NULL;

    // 如果块大小足够大，分割块（仅当剩余空间大于最小块时）
    if (best_fit->size >= size + BLOCK_SIZE + MIN_BLOCK_SIZE) {
        Block* new_block = (Block*)((char*)best_fit + BLOCK_SIZE + size);
        new_block->size = best_fit->size - size - BLOCK_SIZE;
        new_block->is_free = 1;
        new_block->next = best_fit->next;
        new_block->prev = best_fit;

        if (best_fit->next) best_fit->next->prev = new_block;
        best_fit->next = new_block;
        best_fit->size = size;
    }

    // 标记为已分配
    best_fit->is_free = 0;
    return (char*)best_fit + BLOCK_SIZE;
}

// 释放内存
static inline void _free(void* ptr) {
    if (!MemInitialized) MEM_init();
    if (!ptr) return;

    // 检查指针是否在内存池范围内
    if ((char*)ptr < _HeapMemory_ || (char*)ptr >= (_HeapMemory_ + MEMORY_SIZE) ||
        ((char*)ptr - _HeapMemory_) % ALIGNMENT != 0) return;

    // 获取块头
    Block* block = (Block*)((char*)ptr - BLOCK_SIZE);
    block->is_free = 1;

    // 不立即合并，采用延迟合并策略
}

// 重新分配内存
static inline void* _realloc(void* ptr, size_t size) {
    if (!MemInitialized) MEM_init();
    if (!ptr) return _malloc(size);

    Block* block = (Block*)((char*)ptr - BLOCK_SIZE);
    size = ALIGN(size);

    // 如果当前块的大小已经足够，直接返回
    if (block->size >= size) return ptr;

    // 分配新块并拷贝数据
    void* new_ptr = _malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        _free(ptr);
    }
    return new_ptr;
}

// 打印内存状态（用于调试）
static inline void print_memory_state() {
    if (!MemInitialized) MEM_init();
    Block* current = free_list;
    printf("Memory State:\n");
    printf("-------------------------------------------------\n");
    while (current) {
        printf("Block at %p | Size: %zu | %s\n",
               current,
               current->size,
               current->is_free ? "Free" : "Allocated");
        current = current->next;
    }
    printf("-------------------------------------------------\n");
}

void print_memory_usage() {
    if (!MemInitialized) MEM_init();
    delayed_merge();

    size_t used_memory = 0;            // 用户实际使用的内存
    size_t free_memory = 0;            // 实际未使用的内存
    size_t total_used_blocks = 0;      // 包括块头的已分配块总体积
    size_t total_free_blocks = 0;      // 包括块头的空闲块总体积
    size_t block_count = 0;            // 块的总数（包括空闲和已分配）

    Block* current = free_list;

    // 遍历链表统计内存使用情况
    while (current) {
        block_count++;                                     // 块计数，无论是否空闲
        if (current->is_free) {
            free_memory += current->size;                     // 空闲块的用户内存部分
            total_free_blocks += current->size + BLOCK_SIZE;  // 空闲块的总大小（用户内存 + 块头）
        } else {
            used_memory += current->size;                     // 已用块的用户内存部分
            total_used_blocks += current->size + BLOCK_SIZE;  // 已用块的总大小（用户内存 + 块头）
        }
        current = current->next;
    }

    // 打印统计结果
    printf("Memory Usage:\n");
    printf("-------------------------------------------------\n");
    printf("User Used Memory      : %zu bytes\n", used_memory);
    printf("Free Memory           : %zu bytes\n", free_memory);
    printf("Total Used Block Size : %zu bytes (includes headers)\n", total_used_blocks);
    printf("Total Free Block Size : %zu bytes (includes headers)\n", total_free_blocks);
    printf("Block Count           : %zu\n", block_count);
    printf("-------------------------------------------------\n");
}


void* my_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    (void)ud; (void)osize;
    if (nsize == 0) {
        _free(ptr);
        return NULL;
    } else {
        void* newptr = _realloc(ptr, nsize);
        if (newptr == NULL && ptr != NULL && nsize <= osize) {
            return ptr;
        } else
            return newptr;
    }
}
