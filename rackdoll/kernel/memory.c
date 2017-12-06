#include <memory.h>
#include <printk.h>
#include <string.h>
#include <x86.h>


#define PHYSICAL_POOL_PAGES  64
#define PHYSICAL_POOL_BYTES  (PHYSICAL_POOL_PAGES << 12)
#define BITSET_SIZE          (PHYSICAL_POOL_PAGES >> 6)
#define ADDRMASK 0xFFF0000000000FFF
#define OFFSMASK 0x1FF
#define PAGESIZE 4096
#define PAGE_SIZE PAGESIZE
#define PAGE_MASK ADDRMASK

extern __attribute__((noreturn)) void die(void);

static uint64_t bitset[BITSET_SIZE];

static uint8_t pool[PHYSICAL_POOL_BYTES] __attribute__((aligned(0x1000)));


paddr_t alloc_page(void)
{
	size_t i, j;
	uint64_t v;

	for (i = 0; i < BITSET_SIZE; i++) {
		if (bitset[i] == 0xffffffffffffffff)
			continue;

		for (j = 0; j < 64; j++) {
			v = 1ul << j;
			if (bitset[i] & v)
				continue;

			bitset[i] |= v;
			return (((64 * i) + j) << 12) + ((paddr_t) &pool);
		}
	}

	printk("[error] Not enough identity free page\n");
	return 0;
}

void free_page(paddr_t addr)
{
	paddr_t tmp = addr;
	size_t i, j;
	uint64_t v;

	tmp = tmp - ((paddr_t) &pool);
	tmp = tmp >> 12;

	i = tmp / 64;
	j = tmp % 64;
	v = 1ul << j;

	if ((bitset[i] & v) == 0) {
		printk("[error] Invalid page free %p\n", addr);
		die();
	}

	bitset[i] &= ~v;
}


/*
 * Memory model for Rackdoll OS
 *
 * +----------------------+ 0xffffffffffffffff
 * | Higher half          |
 * | (unused)             |
 * +----------------------+ 0xffff800000000000
 * | (impossible address) |
 * +----------------------+ 0x00007fffffffffff
 * | User                 |
 * | (text + data + heap) |
 * +----------------------+ 0x2000000000
 * | User                 |
 * | (stack)              |
 * +----------------------+ 0x40000000
 * | Kernel               |
 * | (valloc)             |
 * +----------------------+ 0x201000
 * | Kernel               |
 * | (APIC)               |
 * +----------------------+ 0x200000
 * | Kernel               |
 * | (text + data)        |
 * +----------------------+ 0x100000
 * | Kernel               |
 * | (BIOS + VGA)         |
 * +----------------------+ 0x0
 *
 * This is the memory model for Rackdoll OS: the kernel is located in low
 * addresses. The first 2 MiB are identity mapped and not cached.
 * Between 2 MiB and 1 GiB, there are kernel addresses which are not mapped
 * with an identity table.
 * Between 1 GiB and 128 GiB is the stack addresses for user processes growing
 * down from 128 GiB.
 * The user processes expect these addresses are always available and that
 * there is no need to map them exmplicitely.
 * Between 128 GiB and 128 TiB is the heap addresses for user processes.
 * The user processes have to explicitely map them in order to use them.
 */


void map_page(struct task *ctx, vaddr_t vaddr, paddr_t paddr)
{
	uint64_t index;
	paddr_t *pml = (paddr_t *)ctx->pgt;
	for(int i = 3; i > 0; i--){
		index = ((vaddr>>12)>>(i*9))&OFFSMASK;
		if(!((pml[index])&1))
			pml[index]= alloc_page();
		pml[index]= pml[index]|7;
		pml = (paddr_t *)(pml[index]&~ADDRMASK);
	}
	index = (vaddr>>12)&OFFSMASK;
	pml[index]= paddr|7;
}


void load_task(struct task *ctx)
{
	uint64_t i, dataSize, bssSize;
	vaddr_t bssBegin;
	paddr_t *pml4, *oldPml4, *pml3, *oldPml3;
	
	ctx->pgt= alloc_page()&~ADDRMASK;
	dataSize = ctx->bss_end_vaddr - ctx->load_vaddr;

	bssBegin = ctx->load_vaddr + (ctx->load_end_paddr - ctx->load_paddr);
	bssSize = ctx->bss_end_vaddr - bssBegin;

	for(i=0; i < dataSize ; i+= PAGESIZE){
		map_page(ctx, ctx->load_vaddr+i, ctx->load_paddr+i);
	}

	memset((void*)ctx->load_end_paddr, 0, bssSize);

	pml4 = (paddr_t *)ctx->pgt;
	oldPml4 = (paddr_t *)store_cr3();

	pml3 = (paddr_t *)(pml4[0]&~ADDRMASK);
	oldPml3 = (paddr_t *)(oldPml4[0]&~ADDRMASK);

	pml3[0] = oldPml3[0];       
}


 /* code nicolas 
void load_task(struct task *ctx)
{

	//old kernel addr
	uint64_t *ptr;
	uint64_t mask_valid = 0x0000000000000027;
	ptr = (uint64_t*) store_cr3();
	ptr = *ptr & 0x000FFFFFFFFFF000;

	//New page table, mapping kernel
	paddr_t pml4 = alloc_page();
	paddr_t pml3 = alloc_page();
	printk("NewPML4 at %p\n", pml4);
	printk("NewPML3 at %p\n", pml3);
	memcpy((paddr_t*)pml3, ptr, 8);
	pml3 |= mask_valid;
	memcpy((paddr_t*)pml4, &pml3, 8);
	ctx->pgt = pml4;

	int i;
	//Mapping code

	for(i = 0; i+ctx->load_paddr < ctx->load_end_paddr; i+=4096)
	{
		map_page(ctx, ctx->load_vaddr + i, ctx->load_paddr + i);
	}

	for(; i + ctx->load_vaddr < ctx->bss_end_vaddr; i+= 4096){
		paddr_t temp = alloc_page();
		memset(temp,0,4096);
		map_page(ctx,ctx->load_vaddr+i,temp);
	}
}
 */


 /* Code de Vincent
void load_task(struct task *ctx)
{
        uint64_t i;
        uint64_t size_data = (ctx->bss_end_vaddr - ctx->load_vaddr);
        paddr_t *bss_paddr = (paddr_t*)ctx->load_end_paddr;
        vaddr_t bss_vaddr = ctx->load_vaddr + (ctx->load_end_paddr - ctx->load_paddr);
        uint64_t size_bss = ctx->bss_end_vaddr - bss_vaddr;

        ctx->pgt = alloc_page() & ~PAGE_MASK;
        for (i = 0; i < size_data; i += PAGE_SIZE) {
                map_page(ctx, ctx->load_vaddr + i, ctx->load_paddr + i);
        }
        memset(ctx->load_end_paddr, 0, size_bss);

        paddr_t *cur_pml4 = (paddr_t *)store_cr3();
        paddr_t *new_pml4 = (paddr_t *)ctx->pgt;
        paddr_t *kernel_pml3 = (paddr_t*)(cur_pml4[0] & ~PAGE_MASK);
        paddr_t *new_pml3 = (paddr_t*)(new_pml4[0] & ~PAGE_MASK);
        new_pml3[0] = kernel_pml3[0];
}

 */

void set_task(struct task *ctx)
{
	load_cr3(ctx->pgt);
}

void mmap(struct task *ctx, vaddr_t vaddr)
{
	map_page(ctx, vaddr, alloc_page());
}

void munmap(struct task *ctx, vaddr_t vaddr)
{
}

void pgfault(struct interrupt_context *ctx)
{
	
	
	printk("Page fault at %p\n", ctx->rip);
	printk("  cr2 = %p\n", store_cr2());
	asm volatile ("hlt");
}

void duplicate_task(struct task *ctx)
{
}
