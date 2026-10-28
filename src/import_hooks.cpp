#include "import_hooks.hpp"
#include <QFile>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <map>
namespace pa {
namespace {
int protectionAt(std::uintptr_t address) {
    QFile maps("/proc/self/maps");
    if (!maps.open(QIODevice::ReadOnly)) return -1;
    for (const auto &line : maps.readAll().split('\n')) {
        unsigned long start=0,end=0;char flags[5]{};
        if (std::sscanf(line.constData(),"%lx-%lx %4s",&start,&end,flags)!=3) continue;
        if (address>=start && address+sizeof(void *)<=end)
            return (flags[0]=='r'?PROT_READ:0)|(flags[1]=='w'?PROT_WRITE:0)|(flags[2]=='x'?PROT_EXEC:0);
    }
    return -1;
}
}
bool ImportHooks::install(const QString &library,const std::vector<Hook> &hooks,QString *error) {
    if (installed()) {*error="上次导入表事务尚未清理，拒绝重复安装。";return false;}
#if !defined(__x86_64__)
    *error="当前实现仅支持 x86-64 ELF。";return false;
#else
    void *handle=dlopen(library.toLocal8Bit().constData(),RTLD_NOW|RTLD_NOLOAD);
    if (!handle) {*error=QString::fromLocal8Bit(dlerror());return false;}
    link_map *map=nullptr;
    if (dlinfo(handle,RTLD_DI_LINKMAP,&map)!=0) {dlclose(handle);*error="无法定位目标 ELF。";return false;}
    const Elf64_Sym *symbols=nullptr;const char *strings=nullptr;
    const Elf64_Rela *relocations=nullptr;std::size_t size=0;bool rela=false;
    for (auto *d=map->l_ld;d->d_tag!=DT_NULL;++d) {
        switch(d->d_tag) {
        case DT_SYMTAB:symbols=reinterpret_cast<const Elf64_Sym *>(d->d_un.d_ptr);break;
        case DT_STRTAB:strings=reinterpret_cast<const char *>(d->d_un.d_ptr);break;
        case DT_JMPREL:relocations=reinterpret_cast<const Elf64_Rela *>(d->d_un.d_ptr);break;
        case DT_PLTRELSZ:size=d->d_un.d_val;break;
        case DT_PLTREL:rela=d->d_un.d_val==DT_RELA;break;
        }
    }
    if (!rela || !symbols || !strings || !relocations || size%sizeof(Elf64_Rela)) {
        dlclose(handle);*error="目标库没有支持的 ELF 导入表。";return false;
    }
    std::vector<Slot> pending;
    for (const auto &hook : hooks) {
        unsigned matches=0;
        for (std::size_t i=0;i<size/sizeof(Elf64_Rela);++i) {
            const auto &r=relocations[i];
            if (ELF64_R_TYPE(r.r_info)!=R_X86_64_JUMP_SLOT || std::strcmp(strings+symbols[ELF64_R_SYM(r.r_info)].st_name,hook.name)) continue;
            auto **slot=reinterpret_cast<void **>(map->l_addr+r.r_offset);
            const auto p=protectionAt(reinterpret_cast<std::uintptr_t>(slot));
            if (p<0 || !(p&PROT_READ) || (p&PROT_EXEC) || reinterpret_cast<std::uintptr_t>(slot)%alignof(void *) || *slot!=hook.original) {
                dlclose(handle);*error="导入槽地址、权限或原函数不匹配；拒绝修改。";return false;
            }
            pending.push_back({slot,*slot,hook.replacement,p});++matches;
        }
        if (matches!=1) {dlclose(handle);*error="目标函数导入槽不是唯一的: "+QString::fromLatin1(hook.name);return false;}
    }
    dlclose(handle);entries=std::move(pending);
    if (!change(true,error)) return false;
    return true;
#endif
}
bool ImportHooks::change(bool installing,QString *error) {
    const auto pageSize=static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    std::map<std::uintptr_t,int> pages;
    for (const auto &s : entries) {
        if (std::atomic_ref<void *>(*s.address).load()!=(patched?s.replacement:s.original)) {
            *error="导入槽被其他模块更改，拒绝覆盖。";return false;
        }
        pages.emplace(reinterpret_cast<std::uintptr_t>(s.address)&~(pageSize-1),s.protection);
    }
    std::vector<std::uintptr_t> writable;
    for (const auto &[page,p] : pages) {
        if (mprotect(reinterpret_cast<void *>(page),pageSize,p|PROT_WRITE)!=0) {
            *error="无法临时写入导入表: "+QString::fromLocal8Bit(std::strerror(errno));
            for (auto opened : writable) mprotect(reinterpret_cast<void *>(opened),pageSize,pages.at(opened));
            return false;
        }
        writable.push_back(page);
    }
    const bool previous=patched;
    for (const auto &s : entries) std::atomic_ref<void *>(*s.address).store(installing?s.replacement:s.original,std::memory_order_release);
    patched=installing;
    bool ok=true;
    for (const auto &[page,p] : pages) if (mprotect(reinterpret_cast<void *>(page),pageSize,p)!=0) ok=false;
    if (!ok) {
        bool canRollback=true;
        for (const auto &[page,p] : pages) if (mprotect(reinterpret_cast<void *>(page),pageSize,p|PROT_WRITE)!=0) canRollback=false;
        if (canRollback) {
            for (const auto &s : entries) std::atomic_ref<void *>(*s.address).store(previous?s.replacement:s.original);
            patched=previous;
        }
        for (const auto &[page,p] : pages) mprotect(reinterpret_cast<void *>(page),pageSize,p);
        *error="导入表内存权限恢复失败，已尝试回滚。";
    }
    return ok;
}
bool ImportHooks::restore(QString *error) {
    if (!installed()) return true;
    if (!change(false,error)) return false;
    entries.clear();return true;
}
}
