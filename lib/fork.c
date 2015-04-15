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
	if ((!(err & FEC_WR)))
		panic("not FEC_WR");
	if ((!(uvpd[PDX(addr)] & PTE_P)) || (!(uvpt[PGNUM(addr)] & PTE_COW)))
		panic("pgfault panic: not PTE_COW");

	
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	r = sys_page_alloc(0, (void *)PFTEMP, PTE_U|PTE_P|PTE_W);
	if (r)
		panic("pgfault: failed to allocate new page");
	addr = (void *)ROUNDDOWN(addr, PGSIZE);
	memmove((void *)PFTEMP, addr, PGSIZE);

	if (sys_page_map(0, (void *)PFTEMP, 0, addr, PTE_U|PTE_P|PTE_W))
		panic("pgfault: remap failed");
	//make sure remap first and then unmap, or the page may be freed before we remap it
	if (sys_page_unmap(0, (void *)PFTEMP))
		panic("pgfault: unmap failed");
	//panic("pgfault not implemented");
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
	int r = 0;

	// LAB 4: Your code here.
	void *addr = (void *)(pn*PGSIZE);

	if ((!(uvpd[PDX(addr)] & PTE_P)) || (!(uvpt[PGNUM(addr)] & (PTE_P|PTE_U))))
		panic("duppage: no original page accessed");

	if ((uvpt[PGNUM(addr)]&PTE_W) || (uvpt[PGNUM(addr)]&PTE_COW)) {
		r = sys_page_map(0, addr, envid, addr, PTE_COW|PTE_U|PTE_P);
		if (r) {
			cprintf("addr:%d\n", addr);
			panic("duppage: couldn't insert target page1:%e", r);
		}
		//r = sys_page_unmap(0, addr);
		r = sys_page_map(0, addr, 0, addr, PTE_COW|PTE_U|PTE_P);
		if (r)
			panic("duppage: couldn't insert target page:2");
	}
	else r = sys_page_map(0, addr, envid, addr, PTE_U|PTE_P);
	if (r)
		panic("duppage: couldn't insert target page:3");

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
	envid_t childid;
	uintptr_t i;
	set_pgfault_handler(pgfault);
	childid = sys_exofork();
	if (childid	< 0)
		panic("fail to allocate child");
	//we are the child, change thisenv and return 0
	if (childid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// above UTOP has been mapped in env_alloc
	for (i = UTEXT; i < UTOP; i += PGSIZE) {
		//cprintf("i:%d ", i/PGSIZE);
		if ((uvpd[PDX(i)] & PTE_P) && (uvpt[PGNUM(i)] & (PTE_P|PTE_U))) {
			if (i == UXSTACKTOP-PGSIZE)
				continue;
		//	cprintf("i:%d ",i/PGSIZE);
			duppage(childid, (unsigned)(i/PGSIZE));
		//	cprintf("i:%d ",i/PGSIZE);
	}
	}
	if (sys_page_alloc(childid, (void *)(UXSTACKTOP-PGSIZE), PTE_U|PTE_P|PTE_W))
		panic("couldn't allocate exception stack");
	extern void _pgfault_upcall(void);
	if(sys_env_set_pgfault_upcall(childid, _pgfault_upcall))
		panic("fail to set child pgfault");

	if (sys_env_set_status(childid, ENV_RUNNABLE))
		panic("unable to set status");
	return childid;
	panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
