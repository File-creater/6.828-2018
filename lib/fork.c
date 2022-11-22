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

    addr = ROUNDDOWN(addr, PGSIZE);

    if(!(err & FEC_WR))
        panic("pgfault error Not FEC_WR");

    if(!(uvpt[(uintptr_t)PGNUM(addr)] & PTE_COW)) {
        panic("pgfault error Not PTE_COW");
	}


    int perm = PTE_P|PTE_U|PTE_W;
    envid_t envid = sys_getenvid();
    if((r = sys_page_alloc(envid, (void*)PFTEMP, perm)) < 0)
        panic("pgfault sys_page_alloc error: %e", r);

    memcpy((void*) PFTEMP, (void*)addr, PGSIZE);

    if((r = sys_page_map(envid, (void*)PFTEMP, envid, addr, perm)) < 0)
        panic("pgfault sys_page_map error: %e", r);

    if((r = sys_page_unmap(envid, (void*)PFTEMP)) < 0)
        panic("pgfault sys_page_unmap error: %e", r);
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
	// cprintf("duppgage %d\n", pn);
	int r;
    envid_t curenvid = sys_getenvid();
    uintptr_t va = pn * PGSIZE;
    int perm = PTE_P|PTE_U;

    if((uvpt[pn] & PTE_W) || (uvpt[pn] & PTE_COW)){
        if ((r = sys_page_map(curenvid, (void*)va, envid, (void*)va, perm | PTE_COW)) < 0) {
            return r;
		} else {
			// cprintf("copy1\n");
		}
		r = sys_page_map(curenvid, (void*)va, curenvid, (void*)va, perm | PTE_COW);
		// cprintf("?\n");
        if (r < 0) {
            return r;
		} else {
			// cprintf("copy2\n");
		}

    }
    else {
        if((r = sys_page_map(curenvid, (void*)va, envid, (void*)va, perm)) < 0) {
            return r;
		} else {
			// cprintf("copy3\n");
		}
	}



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
	int r;

    set_pgfault_handler(pgfault);

    envid_t envid = sys_exofork();

    if (envid < 0) {
        return envid;
	}


    if (envid == 0) {	// fix
        thisenv = &envs[ENVX(sys_getenvid())];
        return 0;
    }

	cprintf("%d\n", PGNUM(USTACKTOP));

    for (uintptr_t i = 0; i < USTACKTOP; i += PGSIZE){
        if (!(uvpd[i >> PDXSHIFT] & PTE_P) || !(uvpt[PGNUM(i)] & PTE_P)) {	// invalid
            continue;
		}

        if ((r = duppage(envid, PGNUM(i))) < 0) {
            return r;
		}

    }

	cprintf("hhh\nn");

    if((r = sys_page_alloc(envid, (void*)(UXSTACKTOP - PGSIZE), PTE_P|PTE_U|PTE_W)) < 0)
        return r;

    extern void _pgfault_upcall(void);
    if((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
        return r;

    if((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
        return r;

    return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
