#include "compatibility.hpp"
#include <QFile>
#include <elf.h>
#include <link.h>
#include <cstring>
#include <cstdint>

namespace pa {
namespace {
QString noteId(const char *bytes, std::size_t length) {
    std::size_t offset=0;
    while (length-offset>=sizeof(Elf64_Nhdr)) {
        Elf64_Nhdr note{};
        std::memcpy(&note,bytes+offset,sizeof(note));offset+=sizeof(note);
        const auto aligned=[](std::uint32_t n) { return (std::uint64_t(n)+3)&~std::uint64_t(3); };
        const auto name=aligned(note.n_namesz), desc=aligned(note.n_descsz);
        if (name>length-offset || desc>length-offset-name) return {};
        if (note.n_type==NT_GNU_BUILD_ID && note.n_namesz==4 && !std::memcmp(bytes+offset,"GNU\0",4))
            return QString::fromLatin1(QByteArray(bytes+offset+name,note.n_descsz).toHex());
        offset+=name+desc;
    }
    return {};
}
struct Identity { QJsonObject result;const void *address=nullptr; };
int inspect(dl_phdr_info *info, std::size_t, void *context) {
    const QString path=QString::fromLocal8Bit(info->dlpi_name);
    auto &identity=*static_cast<Identity *>(context);
    bool target=false;
    if(identity.address)for(int i=0;i<info->dlpi_phnum;++i) {
        const auto &p=info->dlpi_phdr[i];
        const auto address=reinterpret_cast<std::uintptr_t>(identity.address),start=info->dlpi_addr+p.p_vaddr;
        if(p.p_type==PT_LOAD && address>=start && address-start<p.p_memsz)target=true;
    }
    const bool executable=path.isEmpty();
    const bool kwin=path.section('/',-1).startsWith("libkwin.so.");
    if (identity.address ? !target : (!executable && !kwin)) return 0;
    auto &result=identity.result;
    const QString key=target?"image_build_id":executable?"executable_build_id":"kwin_build_id";
    for (int i=0;i<info->dlpi_phnum;++i) {
        const auto &p=info->dlpi_phdr[i];
        if (p.p_type!=PT_NOTE) continue;
        // Only read notes fully contained in a readable, mapped PT_LOAD.
        for (int j=0;j<info->dlpi_phnum;++j) {
            const auto &load=info->dlpi_phdr[j];
            if (load.p_type!=PT_LOAD || !(load.p_flags&PF_R) || p.p_vaddr<load.p_vaddr
                || p.p_vaddr-load.p_vaddr>load.p_memsz || p.p_memsz>load.p_memsz-(p.p_vaddr-load.p_vaddr)) continue;
            const auto id=noteId(reinterpret_cast<const char *>(info->dlpi_addr+p.p_vaddr),p.p_memsz);
            if (!id.isEmpty()) result[key]=id;
        }
    }
    return 0;
}
}
QJsonObject runtimeIdentity() { Identity identity;dl_iterate_phdr(inspect,&identity);return identity.result; }
QString imageBuildId(const void *address) {Identity identity;identity.address=address;dl_iterate_phdr(inspect,&identity);return identity.result["image_build_id"].toString();}
bool runtimeCompatible(const QJsonObject &expected, QString *error) {
    const auto actual=runtimeIdentity();
    for (const auto *key : {"kwin_build_id","executable_build_id"}) {
        if (expected[key].toString().isEmpty() || actual[key]!=expected[key]) {
            if(error)*error="运行中的 KWin 与模块构建不匹配，请重新登录；不会接管输入。";
            return false;
        }
    }
    return true;
}
QString elfBuildId(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    const auto data=file.readAll();
    if(data.size()<qsizetype(sizeof(Elf64_Ehdr)))return {};
    Elf64_Ehdr header{};std::memcpy(&header,data.constData(),sizeof(header));
    if(std::memcmp(header.e_ident,ELFMAG,SELFMAG) || header.e_ident[EI_CLASS]!=ELFCLASS64
       || header.e_ident[EI_DATA]!=ELFDATA2LSB || header.e_phentsize!=sizeof(Elf64_Phdr))return {};
    const auto size=std::uint64_t(data.size());
    if(header.e_phoff>size || std::uint64_t(header.e_phnum)*sizeof(Elf64_Phdr)>size-header.e_phoff)return {};
    for(int i=0;i<header.e_phnum;++i) {
        Elf64_Phdr p{};std::memcpy(&p,data.constData()+header.e_phoff+i*sizeof(p),sizeof(p));
        if(p.p_type!=PT_NOTE || p.p_offset>size || p.p_filesz>size-p.p_offset)continue;
        const auto id=noteId(data.constData()+p.p_offset,p.p_filesz);if(!id.isEmpty())return id;
    }
    return {};
}
}
