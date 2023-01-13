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

static int load(struct pgdir* pd, Inode* inode, usize offset, u64 va, usize len, u64 flags){
	void* ka;
	u64 end = va + len;
	while(va < end){
		auto pte = get_pte(pd, va, true);
		if((*pte & PTE_VALID) == 0){
            vmmap(pd, va, alloc_page_for_user(), flags);
        }
		ka = (void*)P2K(PTE_ADDRESS(*pte));
		usize n = MIN(PAGE_BASE(va)+PAGE_SIZE-va, end-va);
		n = inodes.read(inode, ka + va - PAGE_BASE(va), offset, n);
		if(n != PAGE_SIZE){
			if(n == end-va){
				memset(ka+n, 0, PAGE_SIZE-n);
			}else if(n == PAGE_BASE(va)+PAGE_SIZE-va){
				memset(ka, 0, PAGE_SIZE-n);
			}else return -1;
		}
		va += n;
		offset += n;
	}
	return 0;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
	// TODO
	envp = envp;
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
	if(strncmp((char*)elf.e_ident, ELFMAG, 4) != 0)
		return error(&ctx, inode, NULL);
	
	// Step2
	struct pgdir pd;
	init_pgdir(&pd);
	Elf64_Phdr phdr;
	for(usize offset = elf.e_phoff, i = 0; i < elf.e_phnum; offset += sizeof(Elf64_Phdr), i++){
		if(inodes.read(inode, (u8*)&phdr, offset, sizeof(Elf64_Phdr)) != sizeof(Elf64_Phdr))
			return error(&ctx, inode, &pd);
		if(phdr.p_type != PT_LOAD)
			continue;

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
		// load + COW
		if((phdr.p_flags & PF_R) && (phdr.p_flags & PF_W)){
			s->fp->writable = true;
			s->flags = ST_FILE;
			if(load(&pd, inode, phdr.p_offset, phdr.p_vaddr, phdr.p_filesz, PTE_USER_DATA) != 0)
				return error(&ctx, inode, &pd);
			for(u64 va = PAGE_BASE(phdr.p_vaddr+phdr.p_filesz+PAGE_SIZE-1); va < phdr.p_vaddr+phdr.p_memsz; va+=PAGE_SIZE){
				vmmap(&pd, va, get_zero_page(), PTE_RO | PTE_USER_DATA);
			}
		}else if((phdr.p_flags & PF_R) && (phdr.p_flags & PF_X)){
			s->flags = ST_TEXT | ST_RO;
			if(load(&pd, inode, phdr.p_offset, phdr.p_vaddr, phdr.p_filesz, PTE_USER_DATA | PTE_RO) != 0)
				return error(&ctx, inode, &pd);
		}
		sp = MAX(sp, s->end);
	}
	inodes.unlock(inode);
	inodes.put(&ctx, inode);
	bcache.end_op(&ctx);
	// Step3
	sp = PAGE_BASE(sp+PAGE_SIZE-1);
	struct section* s = kalloc(sizeof(struct section));
	init_sleeplock(&s->sleeplock);
	s->length = 5*PAGE_SIZE;
	s->begin = sp;
	s->end = s->begin + s->length;
	s->flags = ST_STACK;
	_insert_into_list(&pd.section_head, &s->stnode);
	sp = s->end - 64;

	u64 envc = 0;
	char* envpp[32] = {NULL};
	if(envp){
		while(envp[envc]){
			if(envc >= 32) return error(NULL, NULL, &pd);
			sp -= strlen(envp[envc]) + 1;
			copyout(&pd, (void*)sp, envp[envc], strlen(envp[envc]) + 1);
			envpp[envc] = (char*)sp;
			envc++;
		}
	}

	u64 argc = 0;
	char* argvp[32] = {NULL};
	if(argv){
		while(argv[argc]){
			if(argc >= 32) return error(NULL, NULL, &pd);
			sp -= strlen(argv[argc]) + 1;
			copyout(&pd, (void*)sp, argv[argc], strlen(argv[argc]) + 1);
			argvp[argc] = (char*)sp;
			argc++;
		}
	}
	
	sp -= sp%8;
	for(int i = envc; i >= 0; i--){
		sp -= sizeof(char*);
		copyout(&pd, (void*)sp, &envpp[i], sizeof(char*));
	}
	for(int i = argc; i >= 0; i--){
		sp -= sizeof(char*);
		copyout(&pd, (void*)sp, &argvp[i], sizeof(char*));
	}
	sp -= sizeof(u64);
	copyout(&pd, (void*)sp, &argc, sizeof(u64));

	p->ucontext->sp_el0 = sp;
	p->ucontext->x[0] = argc;
	p->ucontext->x[1] = sp + sizeof(u64);
	p->ucontext->elr = elf.e_entry;
	// Final
	free_pgdir(&p->pgdir);
	p->pgdir = pd;
	copy_sections(&pd.section_head, &p->pgdir.section_head);
	_for_in_list(stp, &pd.section_head){
		if(stp == &pd.section_head) continue;
		kfree(container_of(stp, struct section, stnode));
	}
	attach_pgdir(&pd);
	return argc;
}
