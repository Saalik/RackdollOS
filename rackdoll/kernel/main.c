#include <idt.h>                            /* see there for interrupt names */
#include <memory.h>                               /* physical page allocator */
#include <printk.h>                      /* provides printk() and snprintk() */
#include <string.h>                                     /* provides memset() */
#include <syscall.h>                         /* setup system calls for tasks */
#include <task.h>                             /* load the task from mb2 info */
#include <types.h>              /* provides stdint and general purpose types */
#include <vga.h>                                         /* provides clear() */
#include <x86.h>                                    /* access to cr3 and cr2 */
#define SIZE 512
#define MASK 0XFFF0000000000FFF

__attribute__((noreturn))
void die(void)
{
	/* Stop fetching instructions and go low power mode */
	asm volatile ("hlt");

	/* This while loop is dead code, but it makes gcc happy */
	while (1)
		;
}

__attribute__((noreturn))
void main_multiboot2(void *mb2)
{
	clear();                                     /* clear the VGA screen */
	printk("Rackdoll OS\n-----------\n\n");                 /* greetings */

	setup_interrupts();                           /* setup a 64-bits IDT */
	setup_tss();                                  /* setup a 64-bits TSS */
	interrupt_vector[INT_PF] = pgfault;      /* setup page fault handler */

	disable_pic();                         /* disable anoying legacy PIC */
	sti();                                          /* enable interrupts */

	/* Start test 
	struct task fake;
	paddr_t new;
	fake.pgt = store_cr3();
	new = alloc_page();
	map_page(&fake, 0x201000, new);*/
	print_pgt(store_cr3(),4);
	/* Ending */


	load_tasks(mb2);                         /* load the tasks in memory */
	run_tasks();                                 /* run the loaded tasks */

	printk("\nGoodbye!\n");                                 /* fairewell */
	die();                        /* the work is done, we can die now... */
}



void print_pgt (paddr_t pml, uint8_t lvl)
{
	uint64_t *tmp;
	for(int i=0; i<SIZE; i++){
		tmp = (uint64_t *)pml;
		if ((*tmp)&1)
			printk("%x: %x at %d \n", pml, (*tmp)&~MASK, lvl);
		if(!((*tmp)&64) && (*tmp)&1)
			if(lvl!=1)
				print_pgt((*tmp)&~MASK,lvl-1);	
		pml+=sizeof(paddr_t);
	}
}
