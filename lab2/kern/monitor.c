// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <inc/mmu.h> 

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h> 

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};
int mon_showmappings(int argc, char **argv, struct Trapframe *tf);
int mon_setpermission(int argc, char **argv, struct Trapframe *tf);
static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display a backtrace of the function stack", mon_backtrace },
	// { "testprintf", "Test cprintf", mon_testprintf },
	// { "ans", "Answer the question", mon_ans },
	{ "showmappings", "Usage: showmappings <start> <end>", mon_showmappings },
	{ "setpermission", "Usage: setpermission <va> <perm>", mon_setpermission },
	{ "dumpmem", "Usage: dumpmem [opt: -p|-v] <addr> <len>", mon_dumpmem },

};

/***** Implementations of basic kernel monitor commands *****/
int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t *ebp = (uint32_t *)read_ebp();
	while (ebp != 0) {
		//打印ebp, eip, 最近的五个参数
		uint32_t eip = *(ebp + 1);
		cprintf("ebp %08x eip %08x args %08x %08x %08x %08x %08x\n", ebp, eip, *(ebp + 2), *(ebp + 3), *(ebp + 4), *(ebp + 5), *(ebp + 6));
		//打印文件名等信息
		// debuginfo_eip((uintptr_t)eip, &eipdebuginfo);
		// cprintf("%s:%d", eipdebuginfo.eip_file, eipdebuginfo.eip_line);
		// cprintf(": %.*s+%d\n", eipdebuginfo.eip_fn_namelen, eipdebuginfo.eip_fn_name, eipdebuginfo.eip_fn_addr);
		struct Eipdebuginfo info;
		// debuginfo_eip((uintptr_t)eip, &info);
		int ret = debuginfo_eip((uintptr_t)eip, &info);
		
		// 添加调试输出
		cprintf("debuginfo_eip returned: %d\n", ret);
		cprintf("\t%s:%d: %.*s+%d\n", info.eip_file, info.eip_line, info.eip_fn_namelen, info.eip_fn_name, eip-info.eip_fn_addr);
		//更新ebp
		ebp = (uint32_t *)(*ebp);
	}
	return 0;

}

int 
mon_showmappings(int argc, char **argv, struct Trapframe *tf) 
{
    if (argc < 2 || argc > 3 || argv[1] > argv[2]) {
        cprintf("%s\n", commands[3].desc);
		return 0;
	}

    uintptr_t vstart, vend;
    pte_t *pte;

    vstart = (uintptr_t)strtol(argv[1], 0, 0);
	vend = (uintptr_t)strtol(argv[2], 0, 0);

    vstart = ROUNDDOWN(vstart, PGSIZE);
    vend = ROUNDDOWN(vend, PGSIZE);

    for(uintptr_t va = vstart; va <= vend; va += PGSIZE) {
        pte = pgdir_walk(kern_pgdir, (void*)va, 0);
        if (pte && *pte & PTE_P) {
            cprintf("VA: 0x%08x, PA: 0x%08x, U-bit: %d, W-bit: %d\n",
            va, PTE_ADDR(*pte), !!(*pte & PTE_U), !!(*pte & PTE_W));
        } else {
            cprintf("VA: 0x%08x, PA: No Mapping\n", va);
        }
    }
    return 0;
}


int 
mon_setpermission(int argc, char **argv, struct Trapframe *tf) 
{
    if (argc < 2 || argc > 3) {
        cprintf("%s\n", commands[4].desc);
		return 0;
	}
	uintptr_t va = (uintptr_t)strtol(argv[1], 0, 0);
    uint16_t perm = (uint16_t)strtol(argv[2], 0, 0);
	cprintf("Setting permissions 0x%03x for VA 0x%08x\n", perm, va);
	pte_t* pte = pgdir_walk(kern_pgdir, (void*)va, 0);
    if (pte && *pte & PTE_P) {
        *pte = (*pte & ~0xFFF) | (perm & 0xFFF) | PTE_P;
    } else {
        cprintf("No Mapping\n");
    }
    return 0;
}

int 
mon_dumpmem(int argc, char **argv, struct Trapframe *tf) 
{
	// if (argc < 3 || argc > 4) {
    //     cprintf("%s\n", commands[5].desc);
	// 	return 0;
	// }
	// int virtual = true;
    // for (i = 1; i < argc; ++i) {
    //     if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--physical")) {
    //         virtual = false;
    //         break;
    //     }
    // }
	// size_t len = (size_t)strtol(argv[2], 0, 0);
	// if (virtual) {
	// 	uintptr_t vstart = (uintptr_t)strtol(argv[1], 0, 0);
	// 	uintptr_t vend = vstart + len;
	// 	cprintf("Dumping %zu bytes from virtual address 0x%08x\n", len, vstart);
	// 	for 
	// } else {

	// }
    return 0;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
