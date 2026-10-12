struct FileMapping {
    int fd = -1;
    uint64_t bytes = 0;
    uint32_t protection = 0;
    ~FileMapping() { if (fd >= 0) ::close(fd); }
};
struct MappedView { std::shared_ptr<FileMapping> mapping; uint64_t bytes; uint32_t rounded, protection; bool shared; };
std::map<uint32_t,std::shared_ptr<FileMapping>> mappingHandles;
std::map<std::string,std::weak_ptr<FileMapping>> namedMappings;
std::map<uint32_t,MappedView> mappedViews;
uint32_t nextMappingHandle = 0x76000000;
void resetMappings() {
    if (memory) for (const auto& view : mappedViews) memory->unmapFile(view.first, view.second.rounded);
    mappedViews.clear(); mappingHandles.clear(); namedMappings.clear(); nextMappingHandle=0x76000000;
}
bool queryMappedMemory(uint32_t address,uint32_t& allocationBase,uint32_t& allocationProtection,uint64_t& end,uint32_t& type) {
    auto after=mappedViews.upper_bound(address);if(after==mappedViews.begin())return false;
    auto view=std::prev(after);uint64_t limit=uint64_t(view->first)+view->second.rounded;
    if(address>=limit)return false;
    allocationBase=view->first;allocationProtection=view->second.protection;end=std::min(end,limit);type=0x40000;return true;
}
bool mappingDispatch(const std::string& name) {
    if (name == "CloseHandle") {
        auto found=mappingHandles.find(arg(0));if(found==mappingHandles.end())return false;
        mappingHandles.erase(found);ret(1,1);return true;
    }
    if (name == "CreateFileMappingA") {
        std::string objectName=arg(5)?guestString(arg(5)):std::string{};
        std::shared_ptr<FileMapping> mapping;
        if(!objectName.empty()) { auto found=namedMappings.find(objectName);if(found!=namedMappings.end())mapping=found->second.lock(); }
        if(mapping)kernelError(183);
        else {
            uint32_t protection=arg(2)&0xff;
            if((arg(2)&~0x080000ffu)||(protection!=2&&protection!=4&&protection!=8&&protection!=0x20&&protection!=0x40&&protection!=0x80)){
                kernelError(87);ret(0,6);return true;
            }
            mapping=std::make_shared<FileMapping>();mapping->protection=protection;
            uint64_t bytes=(uint64_t(arg(3))<<32)|arg(4);
            if(arg(0)==UINT32_MAX){
                if(!bytes||bytes>INT64_MAX){kernelError(87);ret(0,6);return true;}
                mapping->fd=int(::syscall(__NR_memfd_create,"amrts-mapping",1));
                if(mapping->fd<0){kernelError(fileError());ret(0,6);return true;}
                if(::ftruncate(mapping->fd,off_t(bytes))){kernelError(fileError());ret(0,6);return true;}
                mapping->bytes=bytes;
            }else{
                auto file=fileHandles.find(arg(0));if(file==fileHandles.end()){kernelError(6);ret(0,6);return true;}
                struct stat status{};if(::fstat(file->second,&status)){kernelError(fileError());ret(0,6);return true;}
                if(!bytes)bytes=uint64_t(status.st_size);
                if(!bytes){kernelError(1006);ret(0,6);return true;}
                bool writable=protection==4||protection==0x40;
                if(writable&&(::fcntl(file->second,F_GETFL)&O_ACCMODE)==O_RDONLY){kernelError(5);ret(0,6);return true;}
                if(bytes>uint64_t(status.st_size)&&(!writable||bytes>INT64_MAX||::ftruncate(file->second,off_t(bytes)))){
                    kernelError(5);ret(0,6);return true;
                }
                mapping->fd=::fcntl(file->second,F_DUPFD_CLOEXEC,0);
                if(mapping->fd<0){kernelError(fileError());ret(0,6);return true;}mapping->bytes=bytes;
            }
            if(!objectName.empty())namedMappings[objectName]=mapping;kernelError(0);
        }
        uint32_t handle=nextMappingHandle;nextMappingHandle+=4;mappingHandles.emplace(handle,mapping);ret(handle,6);return true;
    }
    if (name == "MapViewOfFile") {
        auto found=mappingHandles.find(arg(0));if(found==mappingHandles.end()){kernelError(6);ret(0,5);return true;}
        const auto& mapping=found->second;
        uint64_t offset=(uint64_t(arg(2))<<32)|arg(3),bytes=arg(4);
        if(offset%65536||offset>=mapping->bytes){kernelError(1132);ret(0,5);return true;}
        if(!bytes)bytes=mapping->bytes-offset;
        if(bytes>mapping->bytes-offset||bytes>0x0f000000u){kernelError(8);ret(0,5);return true;}
        uint32_t access=arg(1);bool write=(access&2)!=0,copy=(access&1)!=0&&!write;
        if(access!=0x000f001fu&&(access&~0x27u)){kernelError(87);ret(0,5);return true;}
        bool writable=mapping->protection==4||mapping->protection==0x40;
        if(write&&!writable){kernelError(5);ret(0,5);return true;}
        uint32_t page=uint32_t(sysconf(_SC_PAGESIZE)),rounded=uint32_t((bytes+page-1)/page*page);
        uint64_t start=0x60000000;
        for(const auto& view:mappedViews){if(uint64_t(view.first)>=start+rounded)break;
            start=(uint64_t(view.first)+view.second.rounded+65535)/65536*65536;}
        if(start+rounded>0x6f000000){kernelError(8);ret(0,5);return true;}
        uint32_t protection=copy?8:write?4:2;
        if(!memory->mapFile(uint32_t(start),bytes,mapping->fd,offset,!copy,protection)){
            kernelError(fileError());ret(0,5);return true;
        }
        mappedViews.emplace(uint32_t(start),MappedView{mapping,bytes,rounded,protection,!copy});
        __android_log_print(ANDROID_LOG_INFO,"AMRTS","Mapped file view %08x bytes=%llu offset=%llu",uint32_t(start),(unsigned long long)bytes,(unsigned long long)offset);
        ret(uint32_t(start),5);return true;
    }
    if (name == "UnmapViewOfFile") {
        auto view=mappedViews.find(arg(0));if(view==mappedViews.end()){kernelError(487);ret(0,1);return true;}
        bool ok=memory->unmapFile(view->first,view->second.rounded);
        if(ok)mappedViews.erase(view);ret(ok,1);return true;
    }
    if (name == "FlushViewOfFile") {
        auto after=mappedViews.upper_bound(arg(0));if(after==mappedViews.begin()){kernelError(487);ret(0,2);return true;}
        auto view=std::prev(after);uint64_t remaining=uint64_t(view->first)+view->second.bytes-arg(0);
        if(arg(0)<view->first||arg(0)>=uint64_t(view->first)+view->second.bytes||(arg(1)&&arg(1)>remaining)){
            kernelError(487);ret(0,2);return true;
        }
        uint32_t page=uint32_t(sysconf(_SC_PAGESIZE)),start=arg(0)/page*page;
        uint64_t bytes=(arg(1)?arg(1):remaining)+(arg(0)-start);
        if(::msync(memory->address(start,bytes),bytes,MS_SYNC)){kernelError(fileError());ret(0,2);return true;}
        ret(1,2);return true;
    }
    return false;
}
