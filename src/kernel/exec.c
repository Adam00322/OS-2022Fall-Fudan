#include <elf.h>
#include <common/string.h>
#include <common/defines.h>
#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <aarch64/trap.h>
#include <fs/file.h>
#include <fs/inode.h>

//static u64 auxv[][2] = {{AT_PAGESZ, PAGE_SIZE}};
extern int fdalloc(struct file* f);

static int error(OpContext* ctx, Inode* inode, struct pgdir* pd){
	if(inode){
		inodes.unlock(inode);
		inodes.put(ctx, inode);
		bcache.end_op(ctx);
	}
	if(pd){
		free_pgdir(pd);
	}
	return -1;
}

static int load(struct pgdir* pd, Inode* inode, usize offset, u64 va, usize len){
	u64 end = va + len;
	void* ka;
	for(usize i = 0; i <= len;){
		auto pte = get_pte(pd, va+i, true);
		if((*pte & PTE_VALID) == 0){
            vmmap(pd, offset, alloc_page_for_user(), PTE_USER_DATA);
        }
		ka = P2K(PTE_ADDRESS(*pte));
		usize n = inodes.read(inode, ka, offset + i, MIN(PAGE_SIZE, len-i));
		if(n != PAGE_SIZE){
			if(n != len-i)
				return -1;
			memset(ka+n, 0, PAGE_SIZE-n);
		}
	}
	return 0;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
	// TODO
	auto p = thisproc();
	u64 sp = 0;
	OpContext ctx;
	bcache.begin_op(&ctx);
	Inode* inode = namei(path, &ctx);
	if(inode == NULL)
		return error(&ctx, inode, NULL);
	inodes.lock(inode);

	// Step1
	Elf64_Ehdr elf;
	if(inodes.read(inode, (u8*)&elf, 0, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr))
		return error(&ctx, inode, NULL);
	if(strncmp(elf.e_ident, ELFMAG, 4) != 0)
		return error(&ctx, inode, NULL);
	
	// Step2
	struct pgdir pd;
	init_pgdir(&pd);
	Elf64_Phdr phdr;
	for(usize offset = 0; offset < elf.e_phnum; offset += sizeof(Elf64_Phdr)){
		if(inodes.read(inode, (u8*)&phdr, offset, sizeof(Elf64_Phdr)) != sizeof(Elf64_Phdr))
			return error(&ctx, inode, &pd);
		if(phdr.p_type != PT_LOAD)
			continue;
		if(phdr.p_vaddr % PAGE_SIZE != 0)
			return error(&ctx, inode, &pd);

		struct section* s = kalloc(sizeof(struct section));
		init_sleeplock(&s->sleeplock);
		s->begin = phdr.p_vaddr;
		s->end = s->begin + phdr.p_memsz;
		_insert_into_list(&pd.section_head, &s->stnode);
		s->fp = filealloc();
		memset(s->fp, 0, sizeof(struct file));
		s->fp->ip = inode;
		s->fp->type = FD_INODE;
		s->fp->readable = true;
		s->offset = phdr.p_offset;
		s->length = phdr.p_memsz;
		// load
		if(load(&pd, inode, phdr.p_offset, phdr.p_vaddr, phdr.p_filesz) != 0)
			return error(&ctx, inode, &pd);
		// COW
		if(elf.e_flags & (PF_R | PF_W)){
			s->fp->writable = true;
			s->flags = ST_FILE;
			for(u64 va = PAGE_BASE(phdr.p_vaddr+phdr.p_filesz+PAGE_SIZE-1); va < phdr.p_vaddr+phdr.p_memsz; va+=PAGE_SIZE){
				vmmap(&pd, va, get_zero_page(), PTE_RO | PTE_USER_DATA);
			}
		}else if(elf.e_flags & (PF_R | PF_X)){
			s->flags = ST_TEXT | ST_RO;
		}
		sp = MAX(sp, s->end);
	}
	inodes.unlock(inode);
	inodes.put(&ctx, inode);
	bcache.end_op(&ctx);
	// Step3
	u64 sp = PAGE_BASE(sp+PAGE_SIZE-1);
	vmmap(&pd, sp, alloc_page_for_user(), PTE_USER_DATA);
	sp += PAGE_SIZE;
	vmmap(&pd, sp, alloc_page_for_user(), PTE_USER_DATA);
	sp += PAGE_SIZE;

	int argc;
	for(argc = 0; argv[argc]; argc++){
		if(argc >= 32) return error(NULL, NULL, &pd);
		sp -= strlen(argv[argc]) + 1;
	}
	sp -= 1;
	sp -= sizeof(void*) + sizeof(int);
	copyout(&pd, sp, &argc, sizeof(int));
	copyout(&pd, sp + sizeof(int), &argv, sizeof(void*));
	for(u64 i = sp + sizeof(int) + sizeof(void*), j = 0; j<=argc; j++){
		copyout(&pd, i, argv[j], strlen(argv[j]) + 1);
		i += strlen(argv[j]) + 1;
	}
	p->ucontext->sp_el0 = sp;
	p->ucontext->x[0] = argc;
	p->ucontext->x[1] = argv;
	p->ucontext->elr = elf.e_entry;
	// Final
	free_pgdir(&p->pgdir);
	p->pgdir = pd;
	attach_pgdir(&pd);
	return 0;
}
