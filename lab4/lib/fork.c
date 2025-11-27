// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if (!(utf->utf_err & FEC_WR)) {
		panic("pgfault: not a write fault");
	}
	if (!(uvpd[PDX(addr)] & PTE_P)) {
		panic("pgfault: page directory not present");
	}
	if (!(uvpt[PGNUM(addr)] & PTE_P)) {
		panic("pgfault: page not present");
	}
	pte_t pte = uvpt[PGNUM(addr)];
	if (!(pte & PTE_COW)) {
		panic("pgfault: not a copy-on-write page");
	}
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	r = sys_page_alloc(0, PFTEMP, PTE_W | PTE_U | PTE_P);
	if (r < 0) {
		panic("pgfault: sys_page_alloc failed: %e", r);
	}
	memmove(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);
	r = sys_page_map(0, PFTEMP, 0, ROUNDDOWN(addr, PGSIZE), PTE_W | PTE_U | PTE_P);
	if (r < 0) {
		panic("pgfault: sys_page_map failed: %e", r);
	}
	r = sys_page_unmap(0, PFTEMP);
	if (r < 0) {
		panic("pgfault: sys_page_unmap failed: %e", r);
	}
	// panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	pte_t pte = uvpt[pn];
	void *addr = (void *)(pn * PGSIZE);
	if (pte & PTE_W || pte & PTE_COW) {
		// Map the page copy-on-write in the child.
		r = sys_page_map(0, addr, envid, addr, PTE_COW | PTE_U | PTE_P);
		if (r < 0) {
			panic("duppage: sys_page_map failed: %e", r);
		}
		// Remap the page copy-on-write in the parent.
		r = sys_page_map(0, addr, 0, addr, PTE_COW | PTE_U | PTE_P);
		if (r < 0) {
			panic("duppage: sys_page_map failed: %e", r);
		}
	} else {
		// Map the page read-only in the child.
		r = sys_page_map(0, addr, envid, addr, PTE_U | PTE_P);
		if (r < 0) {
			panic("duppage: sys_page_map failed: %e", r);
		}
	}
	// panic("duppage not implemented");
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	set_pgfault_handler(pgfault);
	// Create the child environment.
	envid_t envid = sys_exofork();
	if (envid < 0) {
		panic("fork: sys_exofork failed: %e", envid);
	}
	if (envid == 0) {
		// We're the child.
		// thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	// We're the parent.
	uint32_t addr;
	for (addr = 0; addr < USTACKTOP; addr += PGSIZE) {
		if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P)) {
			int r = duppage(envid, PGNUM(addr));
			if (r < 0) {
				panic("fork: duppage failed: %e", r);
			}
		}
	}
	// Allocate a new page for the child's user exception stack.
	int r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_W | PTE_U | PTE_P);
	if (r < 0) {
		panic("fork: sys_page_alloc failed: %e", r);
	}
	// Set the child's page fault upcall.
	extern void _pgfault_upcall(void);
	r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
	if (r < 0) {
		panic("fork: sys_env_set_pgfault_upcall failed: %e", r);
	}
	// Mark the child as runnable.
	r = sys_env_set_status(envid, ENV_RUNNABLE);
	if (r < 0) {
		panic("fork: sys_env_set_status failed: %e", r);
	}
	return envid;
	// panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	set_pgfault_handler(pgfault);
	// Create the child environment.
	envid_t envid = sys_exofork();
	if (envid < 0) {
		panic("sfork: sys_exofork failed: %e", envid);
	}
	if (envid == 0) {
		// We're the child.
		// thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	// We're the parent.
	uint32_t addr;
	for (addr = 0; addr < USTACKTOP; addr += PGSIZE) {
		if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P)) {
			int r;
			if (addr >= USTACKTOP - PGSIZE) {
				// Stack page: copy-on-write
				r = duppage(envid, PGNUM(addr));
				if (r < 0) {
					panic("sfork: duppage failed: %e", r);
				}
			} else {
				// Non-stack page: shared
				pte_t pte = uvpt[PGNUM(addr)];
				r = sys_page_map(0, (void *)addr, envid, (void *)addr, pte & PTE_SYSCALL);
				if (r < 0) {
					panic("sfork: sys_page_map failed: %e", r);
				}
			}
		}
	}
	// Allocate a new page for the child's user exception stack.
	int r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_W | PTE_U | PTE_P);
	if (r < 0) {
		panic("sfork: sys_page_alloc failed: %e", r);
	}
	// Set the child's page fault upcall.
	extern void _pgfault_upcall(void);
	r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
	if (r < 0) {
		panic("sfork: sys_env_set_pgfault_upcall failed: %e", r);
	}
	// Mark the child as runnable.
	r = sys_env_set_status(envid, ENV_RUNNABLE);
	if (r < 0) {
		panic("sfork: sys_env_set_status failed: %e", r);
	}
	return envid;
	// panic("sfork not implemented");
	// return -E_INVAL;
}
