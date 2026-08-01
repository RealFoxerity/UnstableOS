#ifndef USE_LEGACY_PFA

#include "mm/pmm.h"
#include "mm/kernel_memory.h"

#include <stdbool.h>
#include <string.h>

#include "kernel.h"
#include "multiboot.h"

#define LOWEST_PAGE (PAGE_ALIGN_UP(LOWEST_PHYS_ADDR_ALLOWABLE) / PAGE_SIZE)

// Maximum allocation order: 2^10 = 1024 pages = 4 MiB
#define PMM_ORDER_MAX 10

// Sentinel value, indicates that this is a tail page
// The refcount is instead the page number of the allocation's head page
#define ORDER_TAIL (PMM_ORDER_MAX + 1)

typedef struct __attribute__((packed))
{
	// The reference count of this page
	uint64_t refcount : 20;

	uint64_t free_next : 20;
	uint64_t free_prev : 20;

	// The order of this page, if refcount == 0, the order of the freelist this page is part of
	uint64_t order : 4;
} page_info_t;

#define PAGE_ALIGN_UP(address) ((address + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(address) (address & ~(PAGE_SIZE - 1))

// This is where the page info structs live in memory
#define PAGES_START 0x07000000
#define PAGES_END 0x08000000

typedef struct
{
	size_t start;
	size_t end;
} range_t;

// This structure takes up exactly one page and is freed into the PMM after the VMM is up and running
#define PRE_VMM_RANGES_MAX ((PAGE_SIZE - sizeof(size_t)) / sizeof(range_t))

static struct __attribute__((packed, aligned(PAGE_SIZE)))
{
	range_t ranges[PRE_VMM_RANGES_MAX];
	size_t ranges_count;
} pre_vmm_info;

static bool pre_vmm;

static size_t usable_pages_end, usable_page_count, free_page_count;

// We can safely use 0 as the end-of-freelist sentinel because page 0 is not allowed to be marked as free
static size_t freelists[PMM_ORDER_MAX + 1] = { 0 };
static page_info_t* pages = (page_info_t*)PAGES_START;

static spinlock_t lock = { 0 };

#define PAGE_INFO(page_num) pages[page_num]

#define FREELIST_INSERT(block, order) ({ \
		PAGE_INFO(block).free_prev = 0; \
		PAGE_INFO(block).free_next = freelists[order]; \
		if (freelists[order]) PAGE_INFO(freelists[order]).free_prev = block; \
		freelists[order] = block; \
	})
#define FREELIST_REMOVE(block) ({ \
		if (PAGE_INFO(block).free_prev) PAGE_INFO(PAGE_INFO(block).free_prev).free_next = PAGE_INFO(block).free_next; \
		else freelists[PAGE_INFO(block).order] = PAGE_INFO(block).free_next; \
		if (PAGE_INFO(block).free_next) PAGE_INFO(PAGE_INFO(block).free_next).free_prev = PAGE_INFO(block).free_prev; \
	})

static void pre_vmm_insert_range(const size_t start, const size_t end, range_t* ranges, size_t* ranges_count, const size_t ranges_max)
{
	if (start >= end)
		return;

	size_t i = 0;

	// Find the first range that doesn't end before we start
	// (we touch it, overlap it, or must be inserted before it)
	while (i < *ranges_count && ranges[i].end < start)
		i++;

	// Are we appending to the end *or* NOT merging?
	if (i == *ranges_count || ranges[i].start > end)
	{
		kassert(*ranges_count < ranges_max);

		for (size_t j = *ranges_count; j > i; j--) // Shift all ranges over to make room for the new one
			ranges[j] = ranges[j - 1];

		ranges[i].start = start;
		ranges[i].end = end;
		(*ranges_count)++;
		return;
	}

	// We found a range we overlap with or touch, extend it
	if (start < ranges[i].start)
		ranges[i].start = start;

	if (end > ranges[i].end)
		ranges[i].end = end;

	size_t next = i + 1;

	while (next < *ranges_count && ranges[i].end >= ranges[next].start)
	{
		if (ranges[next].end > ranges[i].end)
			ranges[i].end = ranges[next].end;

		next++;
	}

	const size_t merge_count = next - 1 - i;

	if (merge_count > 0)
	{
		for (size_t j = i + 1; j < *ranges_count - merge_count; j++)
			ranges[j] = ranges[j + merge_count];

		*ranges_count -= merge_count;
	}
}

#define PRE_VMM_RESERVED_RANGES_MAX 32

static void pre_vmm_insert_range_checked(size_t start, const size_t end, const multiboot_info_t* multiboot_info)
{
	if (start < LOWEST_PAGE)
		start = LOWEST_PAGE;

	if (start >= end) // The range did not contain any whole pages
		return;

	range_t reserved[PRE_VMM_RESERVED_RANGES_MAX]; // Let's hope 32 is enough here >_<
	size_t reserved_count = 0;

	// Add the kernel to the list of ranges to avoid
	pre_vmm_insert_range(
		PAGE_ALIGN_DOWN((uintptr_t)KERNEL_START) / PAGE_SIZE,
		PAGE_ALIGN_UP((uintptr_t)KERNEL_END) / PAGE_SIZE,
		reserved, &reserved_count, PRE_VMM_RESERVED_RANGES_MAX);

	// Protect the multiboot structure itself
	pre_vmm_insert_range(
		PAGE_ALIGN_DOWN((uintptr_t)multiboot_info) / PAGE_SIZE,
		PAGE_ALIGN_UP((uintptr_t)multiboot_info + sizeof(multiboot_info_t)) / PAGE_SIZE,
		reserved, &reserved_count, PRE_VMM_RESERVED_RANGES_MAX);

	// ...and the command line
	if (multiboot_info->flags & MULTIBOOT_INFO_CMDLINE)
	{
		pre_vmm_insert_range(
			PAGE_ALIGN_DOWN((uintptr_t)multiboot_info->cmdline) / PAGE_SIZE,
			PAGE_ALIGN_UP((uintptr_t)multiboot_info->cmdline + strlen((char*)multiboot_info->cmdline) + 1) / PAGE_SIZE,
			reserved, &reserved_count, PRE_VMM_RESERVED_RANGES_MAX);
	}

	// ...and modules
	if (multiboot_info->flags & MULTIBOOT_INFO_MODS)
	{
		const multiboot_module_t* mods = (const multiboot_module_t*)multiboot_info->mods_addr;
		for (size_t i = 0; i < multiboot_info->mods_count; i++)
		{
			pre_vmm_insert_range(
				PAGE_ALIGN_DOWN((uintptr_t)mods[i].mod_start) / PAGE_SIZE,
				PAGE_ALIGN_UP((uintptr_t)mods[i].mod_end) / PAGE_SIZE,
				reserved, &reserved_count, PRE_VMM_RESERVED_RANGES_MAX);

			pre_vmm_insert_range(
				PAGE_ALIGN_DOWN((uintptr_t)mods[i].cmdline) / PAGE_SIZE,
				PAGE_ALIGN_UP((uintptr_t)mods[i].cmdline + strlen((char*)mods[i].cmdline) + 1) / PAGE_SIZE,
				reserved, &reserved_count, PRE_VMM_RESERVED_RANGES_MAX);
		}

		pre_vmm_insert_range(
			PAGE_ALIGN_DOWN((uintptr_t)multiboot_info->mods_addr) / PAGE_SIZE,
			PAGE_ALIGN_UP((uintptr_t)multiboot_info->mods_addr + multiboot_info->mods_count * sizeof(multiboot_module_t)) / PAGE_SIZE,
			reserved, &reserved_count, PRE_VMM_RESERVED_RANGES_MAX);
	}

	// ...and the bootloader name
	if (multiboot_info->flags & MULTIBOOT_INFO_BOOT_LOADER_NAME)
	{
		pre_vmm_insert_range(
			PAGE_ALIGN_DOWN((uintptr_t)multiboot_info->boot_loader_name) / PAGE_SIZE,
			PAGE_ALIGN_UP((uintptr_t)multiboot_info->boot_loader_name + strlen((char*)multiboot_info->boot_loader_name) + 1) / PAGE_SIZE,
			reserved, &reserved_count, PRE_VMM_RESERVED_RANGES_MAX);
	}

	// ...and the drive info
	if (multiboot_info->flags & MULTIBOOT_INFO_DRIVE_INFO)
	{
		pre_vmm_insert_range(
			PAGE_ALIGN_DOWN((uintptr_t)multiboot_info->drives_addr) / PAGE_SIZE,
			PAGE_ALIGN_UP((uintptr_t)multiboot_info->drives_addr + multiboot_info->drives_length) / PAGE_SIZE,
			reserved, &reserved_count, PRE_VMM_RESERVED_RANGES_MAX);
	}

	// ...and the symbol tables
	if (multiboot_info->flags & MULTIBOOT_INFO_ELF_SHDR)
	{
		pre_vmm_insert_range(
			PAGE_ALIGN_DOWN((uintptr_t)multiboot_info->u.elf_sec.addr) / PAGE_SIZE,
			PAGE_ALIGN_UP((uintptr_t)multiboot_info->u.elf_sec.addr + multiboot_info->u.elf_sec.num * multiboot_info->u.elf_sec.size) / PAGE_SIZE,
			reserved, &reserved_count, PRE_VMM_RESERVED_RANGES_MAX);
	}

	for (size_t i = 0; i < reserved_count; i++)
	{
		// Reserved block is entirely behind the range, next.
		if (reserved[i].end <= start)
			continue;

		// Reserved block is entirely in front of the range, done checking!
		if (reserved[i].start >= end)
			break;

		// There is some free memory before the reserved block
		if (reserved[i].start > start)
			pre_vmm_insert_range(start, reserved[i].start, pre_vmm_info.ranges, &pre_vmm_info.ranges_count, PRE_VMM_RANGES_MAX);

		// Move past the block
		if (reserved[i].end > start)
			start = reserved[i].end;
	}

	// Add any remaining space
	if (start < end)
		pre_vmm_insert_range(start, end, pre_vmm_info.ranges, &pre_vmm_info.ranges_count, PRE_VMM_RANGES_MAX);
}

#define BLOCK_BUDDY(block, order) ((block) ^ (1ULL << (order)))

static void insert_free_block(size_t block, size_t order)
{
	// Merge blocks until max order is reached
	while (order < PMM_ORDER_MAX)
	{
		const size_t buddy = BLOCK_BUDDY(block, order);
		if (buddy >= usable_pages_end)
			break;

		const page_info_t* buddy_info = &PAGE_INFO(buddy);

		// If the buddy is not free we can stop merging
		if (buddy_info->refcount != 0 || buddy_info->order != order)
			break;

		FREELIST_REMOVE(buddy);

		// Get the merged block (lower of block and buddy)
		if (buddy < block)
			block = buddy;

		order++;
	}

	// Insert the new block
	FREELIST_INSERT(block, order);

	const size_t page_count = 1ULL << order;

	// Mark the entire block as free with the proper order
	for (size_t i = 0; i < page_count; i++)
	{
		pages[block + i].refcount = 0;
		pages[block + i].order = order;
	}
}

static void insert_free_range(range_t range)
{
	while (range.start < range.end)
	{
		const size_t length = range.end - range.start;

		// Identify the highest order this range fits into
		size_t order = 0;
		while (order < PMM_ORDER_MAX)
		{
			const size_t next_order = order + 1;

			// Abort if the range's start is not correctly aligned for this order
			if ((size_t)(range.start == 0 ? 0 : __builtin_ctz(range.start)) < next_order)
				break;

			// Abort if this order's block length exceeds the number of blocks remaining in the range
			const size_t next_length = 1ULL << next_order;
			if (next_length > length)
				break;

			order = next_order;
		}

		insert_free_block(range.start, order);
		range.start += 1ULL << order;
	}
}

void pmm_init_pre_vmm(const multiboot_info_t* multiboot_info)
{
	pre_vmm = true;
	pre_vmm_info.ranges_count = 0;

	const volatile multiboot_memory_map_t* entry;

	for (size_t entry_offset = 0; entry_offset < multiboot_info->mmap_length; entry_offset += entry->size + sizeof(entry->size))
	{
		entry = (multiboot_memory_map_t*)(multiboot_info->mmap_addr + entry_offset);

		if (entry->type != MULTIBOOT_MEMORY_AVAILABLE)
			continue;

		// Find page-aligned start (inclusive) and end (exclusive) of this entry
		size_t start_page = PAGE_ALIGN_UP(entry->addr) / PAGE_SIZE;
		size_t end_page = PAGE_ALIGN_DOWN(entry->addr + entry->len) / PAGE_SIZE;

		// This range is entirely outside the 32-bit address range
		if (start_page > UINT32_MAX / PAGE_SIZE + 1)
			continue;

		// This range exceeds the 32-bit address range, limit it to that range
		if (end_page > UINT32_MAX / PAGE_SIZE + 1)
			end_page = UINT32_MAX / PAGE_SIZE + 1;

		pre_vmm_insert_range_checked(start_page, end_page, multiboot_info);
	}

	for (size_t i = 0; i < pre_vmm_info.ranges_count; i++)
	{
		const range_t* range = pre_vmm_info.ranges + i;

		if (range->end > usable_pages_end)
			usable_pages_end = range->end;

		usable_page_count += range->end - range->start;
	}

	free_page_count = usable_page_count;
}

void pmm_init_post_vmm()
{
	kassert(pre_vmm);

	const size_t pages_end = PAGES_START + usable_pages_end * sizeof(page_info_t);
	kassert(pages_end <= PAGES_END);

	// Map the page info structure into the address space
	for (uintptr_t virt_addr = PAGES_START; virt_addr < pages_end; virt_addr += PAGE_SIZE)
		paging_map_phys_addr((void*)(pmm_alloc(0) * PAGE_SIZE), (void*)virt_addr, PTE_PDE_PAGE_WRITABLE);

	// Initialize page info
	for (size_t i = 0; i < usable_pages_end; i++)
	{
		pages[i] = (page_info_t) {
			// All pages are marked used initially
			.order = 0,
			.refcount = 1
		};
	}

	// Mark available ranges as free
	for (size_t i = 0; i < pre_vmm_info.ranges_count; i++)
		insert_free_range(pre_vmm_info.ranges[i]);

	pre_vmm = false;
}

static inline __attribute__((always_inline)) size_t pmm_pre_vmm_alloc()
{
	while (true)
	{
		kassert(pre_vmm_info.ranges_count > 0);
		range_t* range = pre_vmm_info.ranges + (pre_vmm_info.ranges_count - 1);
		if (range->start >= range->end)
		{
			pre_vmm_info.ranges_count--;
			continue;
		}

		free_page_count--;
		return range->start++;
	}
}

size_t pmm_alloc(const size_t order)
{
	if (pre_vmm)
	{
		kassert(order == 0);
		return pmm_pre_vmm_alloc();
	}

	kassert(order <= PMM_ORDER_MAX);

	spinlock_acquire(&lock);

	// Find the next order >= 'order' that has available blocks
	size_t block_order = order;
	while (block_order <= PMM_ORDER_MAX && !freelists[block_order])
		block_order++;

	// Out of memory
	if (block_order > PMM_ORDER_MAX)
	{
		spinlock_release(&lock);
		return 0;
	}

	// Found a block!
	const size_t block = freelists[block_order];
	FREELIST_REMOVE(block);

	// Split up the block!
	while (block_order > order)
	{
		// We want to turn a block of order N into two blocks of order N-1
		block_order--;

		// Figure out how many pages a block in this order has
		const size_t order_pages = 1ULL << block_order;

		// Get the block's buddy in the new order
		const size_t buddy = block + (1ULL << block_order);

		// Set the block's pages' orders
		for (size_t i = 0; i < order_pages; i++)
		{
			kassert(pages[buddy + i].refcount == 0);
			pages[buddy + i].order = block_order;
		}

		// Insert into the freelist!
		FREELIST_INSERT(buddy, block_order);
	}

	pages[block].refcount = 1;
	pages[block].order = order;

	const size_t order_pages = 1ULL << order;
	for (size_t i = 1; i < order_pages; i++)
	{
		pages[block + i].order = ORDER_TAIL;
		pages[block + i].refcount = block;
	}

	free_page_count -= order_pages;

	spinlock_release(&lock);

	return block;
}

void pmm_free(const size_t page_num)
{
	kassert(!pre_vmm);

	// NULL free => no-op
	if (page_num == 0)
		return;

	page_info_t* info = &PAGE_INFO(page_num);

	kassert(page_num < usable_pages_end); // Page out of bounds!

	spinlock_acquire(&lock);

	kassert(info->refcount != 0); // Page not allocated!
	kassert(info->order != ORDER_TAIL); // Page not head!
	info->refcount--;

	if (info->refcount == 0)
	{
		const size_t order = info->order;
		free_page_count += 1ULL << order;
		insert_free_block(page_num, order);
	}

	spinlock_release(&lock);
}

void pmm_retain(const size_t page_num)
{
	kassert(!pre_vmm);

	// NULL free => no-op
	if (page_num == 0)
		return;

	page_info_t* info = &PAGE_INFO(page_num);

	kassert(page_num < usable_pages_end); // Page out of bounds!

	spinlock_acquire(&lock);

	kassert(info->refcount != 0); // Page not allocated!
	kassert(info->order != ORDER_TAIL); // Page not head!
	info->refcount++;

	spinlock_release(&lock);
}

size_t pmm_get_usable_pages_end()
{
	return usable_pages_end;
}

size_t pmm_get_usable_page_count()
{
	return usable_page_count;
}

size_t pmm_get_free_page_count()
{
	return free_page_count;
}

// Satisfy the old interface

unsigned long pf_get_free_memory()
{
	return pmm_get_free_page_count() * 4096;
}

void* pfalloc()
{
	return (void*)(pmm_alloc(0) * PAGE_SIZE);
}

void* pfalloc_1M()
{
	return (void*)(pmm_alloc(pmm_size_to_order(1 * 1024 * 1024)) * PAGE_SIZE);
}

void pffree(void* page)
{
	pmm_free((uintptr_t)page / PAGE_SIZE);
}

void pffree_1M(void* block_1M_start)
{
	pmm_free((uintptr_t)block_1M_start / PAGE_SIZE);
}

void* pfalloc_dup_page(void* page)
{
	void* new_frame = pfalloc();
	kassert(new_frame);
	void* mapped_new = paging_map_phys_addr_unspecified(new_frame, PTE_PDE_PAGE_WRITABLE);
	kassert(mapped_new);
	void* mapped_old = paging_map_phys_addr_unspecified(page, PTE_PDE_PAGE_WRITABLE);
	kassert(mapped_old);

	memcpy(mapped_new, mapped_old, PAGE_SIZE);
	paging_unmap_page(mapped_new);
	paging_unmap_page(mapped_old);

	return new_frame;
}

void* pfalloc_ref_inc(void* page)
{
	pmm_retain((uintptr_t)page / PAGE_SIZE);
	return page;
}

#endif
