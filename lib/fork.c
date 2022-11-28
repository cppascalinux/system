// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

extern void _pgfault_upcall(void);

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
	if(!(err&FEC_WR))
		panic("page fault not caused by write");
	pde_t *pde=PGADDR(0x3BD,0x3BD,PDX(addr)<<2);
	if(!(*pde&PTE_P))
		panic("page table doesn't exist");
	if(!(*pde&PTE_U))
		panic("page table cannot be accessed by user");
	if(!(*pde&PTE_W))
		panic("page table is read-only");
	pte_t *pte=PGADDR(0x3BD,PDX(addr),PTX(addr)<<2);
	if(!(*pte&PTE_P))
		panic("page does't exist");
	if(!(*pte&PTE_U))
		panic("page cannot be accessed by user");
	if(!(*pte&PTE_COW))
		panic("page is not marked as copy on write");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	uintptr_t pgb=ROUNDDOWN(addr,PGSIZE);
	sys_page_alloc(0,PFTEMP,PTE_P|PTE_U|PTE_W);
	memmove(PFTEMP,pgb,PGSIZE);
	sys_page_map(0,PFTEMP,0,pgb,PTE_P|PTE_U|PTE_W);

	// LAB 4: Your code here.

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
	uintptr_t addr=pn*PGSIZE;
	pde_t *pde=PGADDR(0x3BD,0x3BD,PDX(addr)<<2);
	if(!(*pde&PTE_P))
		return 0;
	if(!(*pde&PTE_U))
		panic("page table cannot be accessed by user");
	pte_t *pte=PGADDR(0x3BD,PDX(addr),PTX(addr)<<2);
	if(!(*pte&PTE_P))
		return 0;
	if(!(*pte&PTE_U))
		panic("page cannot be accessed by user");
	int perm=*pte&PTE_SYSCALL;
	int ret;
	if(perm&PTE_SHARE)
	{
		if((ret=sys_page_map(0,addr,envid,addr,perm))<0)
			panic("sys_page_map failed %e",ret);
		return 0;
	}
	if(perm&PTE_W)
	{
		perm^=PTE_W;
		perm|=PTE_COW;
	}
	if((ret=sys_page_map(0,addr,envid,addr,perm))<0)
		panic("sys_page_map failed %e",ret);
	if(perm&PTE_COW)
		if(sys_page_map(0,addr,0,addr,perm)<0)
			panic("sys_page_map failed");
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
	envid_t cid=sys_exofork();
	if(cid)
	{
		for(size_t i=0;i*PGSIZE<UTOP;i++)
			if(i*PGSIZE!=UXSTACKTOP-PGSIZE)
				duppage(cid,i);
		sys_page_alloc(cid,UXSTACKTOP-PGSIZE,PTE_U|PTE_P|PTE_W);
		sys_env_set_pgfault_upcall(cid,_pgfault_upcall);
		sys_env_set_status(cid,ENV_RUNNABLE);
	}
	else
	{
		thisenv=(struct Env*)UENVS+ENVX(sys_getenvid());
	}
	return cid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
