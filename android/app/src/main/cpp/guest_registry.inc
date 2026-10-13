std::map<uint32_t,std::string> registryKeys;
uint32_t nextRegistryKey=0x77000000;
void resetRegistry(){registryKeys.clear();nextRegistryKey=0x77000000;}
bool registryDispatch(const std::string& name){
    if(name=="RegCreateKeyExA"){
        uint32_t parent=arg(0);std::string path;
        if(parent>=0x80000000&&parent<=0x80000006)path=hex(parent);
        else if(registryKeys.count(parent))path=registryKeys.at(parent);
        else {ret(6,9);return true;}
        path+="\\"+lower(guestString(arg(1)));
        uint32_t key=nextRegistryKey++;registryKeys.emplace(key,path);write32(arg(7),key);if(arg(8))write32(arg(8),1);
        ret(0,9);return true;
    }
    if(name=="RegCloseKey"){ret(registryKeys.erase(arg(0))?0:6,1);return true;}
    if(name=="RegSetValueExA"){
        auto key=registryKeys.find(arg(0));if(key==registryKeys.end()){ret(6,6);return true;}
        std::string value=arg(1)?guestString(arg(1)):"";uint32_t bytes=arg(5);
        if(bytes>1048576){ret(87,6);return true;}
        const void* data=bytes?kernelPtr(arg(4),bytes):nullptr;
        std::error_code error;std::filesystem::create_directories(std::filesystem::path(hostGameRoot)/"user",error);
        std::string file=(std::filesystem::path(hostGameRoot)/"user/android-registry.bin").string();
        FILE* output=std::fopen(file.c_str(),"ab");if(!output){ret(5,6);return true;}
        uint32_t header[4]={uint32_t(key->second.size()),uint32_t(value.size()),arg(3),bytes};
        bool ok=std::fwrite(header,sizeof header,1,output)==1&&std::fwrite(key->second.data(),1,header[0],output)==header[0]&&
            std::fwrite(value.data(),1,header[1],output)==header[1]&&(!bytes||std::fwrite(data,1,bytes,output)==bytes);
        if(std::fclose(output))ok=false;ret(ok?0:29,6);return true;
    }
    return false;
}
