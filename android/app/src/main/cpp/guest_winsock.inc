// Winsock 2 for StyxNet (styxnet/win32_socket.cpp), over Android BSD sockets.
//
// The game loads ws2_32.dll with LoadLibrary and resolves 31 functions by name
// (table at 0x0069E820). Guest SOCKETs are small handles mapped to host fds.
// WSAEventSelect is emulated by polling the sockets from progressCheck (and
// therefore from every blocking wait), setting the associated guest event and
// latching FD_* bits until WSAEnumNetworkEvents reads them.
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
constexpr uint32_t FD_READ_=1,FD_WRITE_=2,FD_OOB_=4,FD_ACCEPT_=8,FD_CONNECT_=16,FD_CLOSE_=32;
struct GuestSocket {
    // `armed`: Winsock reports each FD_* once and re-enables it only after
    // the matching call (recv for FD_READ, accept for FD_ACCEPT, ...).
    int fd=-1,type=1;uint32_t event=0,mask=0,pending=0,armed=0x3f;
    // An unconnected TCP socket polls as POLLHUP on Linux: only connected
    // stream sockets may report FD_CLOSE.
    bool listening=false,connecting=false,connected=false,writeArmed=true,closed=false;
    int32_t errors[10]{};
};
std::map<uint32_t,GuestSocket> guestSockets;
uint32_t nextSocketHandle=0x400,winsockStatic=0,winsockAsync=0x1000;
const int netDebug=[]{char value[PROP_VALUE_MAX]{};__system_property_get("debug.amrts.net",value);return std::atoi(value);}();
void resetWinsock(){for(auto& item:guestSockets)if(item.second.fd>=0)::close(item.second.fd);guestSockets.clear();nextSocketHandle=0x400;winsockStatic=0;winsockAsync=0x1000;}
uint32_t wsaError(int error){
    switch(error){
        case EWOULDBLOCK:return 10035;case EINPROGRESS:return 10035;case EALREADY:return 10037;case ENOTSOCK:return 10038;
        case EDESTADDRREQ:return 10039;case EMSGSIZE:return 10040;case EPROTOTYPE:return 10041;case ENOPROTOOPT:return 10042;
        case EPROTONOSUPPORT:return 10043;case EOPNOTSUPP:return 10045;case EAFNOSUPPORT:return 10047;case EADDRINUSE:return 10048;
        case EADDRNOTAVAIL:return 10049;case ENETDOWN:return 10050;case ENETUNREACH:return 10051;case ECONNABORTED:return 10053;
        case ECONNRESET:return 10054;case ENOBUFS:return 10055;case EISCONN:return 10056;case ENOTCONN:return 10057;
        case ETIMEDOUT:return 10060;case ECONNREFUSED:return 10061;case EHOSTUNREACH:return 10065;case EACCES:return 10013;
        case EINVAL:return 10022;case EMFILE:return 10024;default:return 10022;
    }
}
GuestSocket* socketOf(uint32_t handle){auto found=guestSockets.find(handle);return found==guestSockets.end()?nullptr:&found->second;}
// Windows and Linux share struct sockaddr_in for AF_INET (16 bytes, BE port/address).
bool guestAddress(uint32_t address,uint32_t length,sockaddr_in& out){
    if(!address||length<16)return false;std::memcpy(&out,kernelPtr(address,16),16);out.sin_family=AF_INET;return true;
}
void putAddress(uint32_t address,uint32_t lengthAddress,const sockaddr_in& value){
    if(!address)return;std::memcpy(kernelPtr(address,16,true),&value,16);
    uint16_t family=2;std::memcpy(kernelPtr(address,2,true),&family,2);if(lengthAddress)write32(lengthAddress,16);
}
void signalSocket(GuestSocket& socket,uint32_t bits){
    bits&=socket.mask&socket.armed;socket.armed&=~bits;
    socket.pending|=bits;
    if(socket.pending&&socket.event){auto found=syncHandles.find(socket.event);if(found!=syncHandles.end())found->second->signaled=true;}
}
// Poll every event-selected socket; called from progressCheck and waits.
void winsockPoll(){
    if(guestSockets.empty())return;
    std::vector<pollfd> fds;std::vector<uint32_t> handles;
    for(auto& item:guestSockets){auto& socket=item.second;if(socket.fd<0||!socket.mask||socket.closed)continue;
        if(socket.type==1&&!socket.listening&&!socket.connecting&&!socket.connected)continue;
        short events=0;if(socket.mask&(FD_READ_|FD_ACCEPT_|FD_CLOSE_))events|=POLLIN;if(socket.connecting||socket.writeArmed)events|=POLLOUT;
        fds.push_back({socket.fd,events,0});handles.push_back(item.first);}
    if(fds.empty()||::poll(fds.data(),fds.size(),0)<=0)return;
    for(size_t i=0;i<fds.size();++i){
        auto& socket=guestSockets[handles[i]];short revents=fds[i].revents;if(!revents)continue;
        if(socket.connecting&&(revents&(POLLOUT|POLLERR|POLLHUP))){
            socket.armed|=FD_CONNECT_|FD_WRITE_;
            int error=0;socklen_t size=sizeof error;getsockopt(socket.fd,SOL_SOCKET,SO_ERROR,&error,&size);
            socket.connecting=false;socket.connected=!error;socket.errors[4]=error?int32_t(wsaError(error)):0;signalSocket(socket,FD_CONNECT_|(error?0:FD_WRITE_));
            socket.writeArmed=false;continue;
        }
        if(revents&POLLIN){
            if(socket.listening)signalSocket(socket,FD_ACCEPT_);
            else if(socket.type==1){
                char probe;ssize_t peek=::recv(socket.fd,&probe,1,MSG_PEEK|MSG_DONTWAIT);
                if(peek==0){socket.closed=true;signalSocket(socket,FD_CLOSE_);}else signalSocket(socket,FD_READ_);
            }else signalSocket(socket,FD_READ_);
        }
        if((revents&(POLLHUP|POLLERR))&&socket.type==1&&!socket.listening){socket.closed=true;socket.errors[5]=10054;signalSocket(socket,FD_CLOSE_);}
        if((revents&POLLOUT)&&socket.writeArmed){socket.writeArmed=false;signalSocket(socket,FD_WRITE_);}
    }
}
uint32_t winsockFail(uint32_t error,uint32_t value=UINT32_MAX){kernelError(error);return value;}
// Sum of a WSABUF array (len, buf pointer pairs).
std::vector<uint8_t> gatherBuffers(uint32_t buffers,uint32_t count){
    std::vector<uint8_t> data;
    for(uint32_t i=0;i<count;++i){uint32_t length=read32(buffers+i*8),pointer=read32(buffers+i*8+4);
        auto* bytes=static_cast<const uint8_t*>(kernelPtr(pointer,length));data.insert(data.end(),bytes,bytes+length);}
    return data;
}
uint32_t scatterBuffers(uint32_t buffers,uint32_t count,const uint8_t* data,uint32_t size){
    uint32_t copied=0;
    for(uint32_t i=0;i<count&&copied<size;++i){uint32_t length=read32(buffers+i*8),pointer=read32(buffers+i*8+4);
        uint32_t chunk=std::min(length,size-copied);std::memcpy(kernelPtr(pointer,chunk,true),data+copied,chunk);copied+=chunk;}
    return copied;
}
// Overlapped completion is reported synchronously: the call either finished
// or failed with WSAEWOULDBLOCK; the overlapped event is signalled at once.
void completeOverlapped(uint32_t overlapped,uint32_t bytes){
    if(!overlapped)return;write32(overlapped,0);write32(overlapped+4,bytes);
    auto found=syncHandles.find(read32(overlapped+16));if(found!=syncHandles.end())found->second->signaled=true;
}
int mapSocketOption(uint32_t level,uint32_t option,int& hostLevel,int& hostOption){
    if(level==0xffff){hostLevel=SOL_SOCKET;
        switch(option){case 4:hostOption=SO_REUSEADDR;break;case 8:hostOption=SO_KEEPALIVE;break;case 0x20:hostOption=SO_BROADCAST;break;
            case 0x80:hostOption=SO_LINGER;break;case 0x1001:hostOption=SO_SNDBUF;break;case 0x1002:hostOption=SO_RCVBUF;break;
            case 0x1007:hostOption=SO_ERROR;break;case 0x1008:hostOption=SO_TYPE;break;case 0x1005:hostOption=SO_SNDTIMEO;break;case 0x1006:hostOption=SO_RCVTIMEO;break;
            case 0xffffff7fu:return 0;// SO_DONTLINGER (~SO_LINGER): Linux closes gracefully by default
            default:return -1;}
        return 1;}
    if(level==6&&option==1){hostLevel=IPPROTO_TCP;hostOption=TCP_NODELAY;return 1;}
    if(level==0){hostLevel=IPPROTO_IP;
        switch(option){case 4:hostOption=IP_TTL;break;case 9:hostOption=IP_MULTICAST_IF;break;case 10:hostOption=IP_MULTICAST_TTL;break;
            case 11:hostOption=IP_MULTICAST_LOOP;break;case 12:hostOption=IP_ADD_MEMBERSHIP;break;case 13:hostOption=IP_DROP_MEMBERSHIP;break;default:return -1;}
        return 1;}
    return -1;
}
void writeProtocolInfo(uint32_t output,int type,int protocol){
    std::memset(kernelPtr(output,372,true),0,372);
    write32(output,type==1?0x20066:0x20609);// XP1_GUARANTEED_DELIVERY|ORDERLY|... / CONNECTIONLESS|BROADCAST|MULTIPOINT
    write32(output+36,type==1?1001:1002);write32(output+40,1);// catalog entry, chain length (base provider)
    write32(output+72,2);write32(output+76,2);write32(output+80,16);write32(output+84,16);// version, AF_INET, max/min sockaddr
    write32(output+88,type);write32(output+92,protocol);write32(output+100,0);write32(output+108,type==1?0:65467);
    const char* label=type==1?"MSAFD Tcpip [TCP/IP]":"MSAFD Tcpip [UDP/IP]";std::memcpy(kernelPtr(output+116,std::strlen(label)+1,true),label,std::strlen(label)+1);
}
// HOSTENT laid out inside the caller's buffer (WSAAsyncGetHostBy*).
uint32_t writeHostEntry(uint32_t buffer,uint32_t size,const std::string& name,uint32_t address){
    uint32_t need=16+16+4*4+uint32_t(name.size())+1;if(size<need)return 10055;
    uint32_t aliases=buffer+16,list=aliases+4,addr=list+8,text=addr+4;
    write32(buffer,text);write32(buffer+4,aliases);uint16_t pair[2]={2,4};std::memcpy(kernelPtr(buffer+8,4,true),pair,4);write32(buffer+12,list);
    write32(aliases,0);write32(list,addr);write32(list+4,0);std::memcpy(kernelPtr(addr,4,true),&address,4);
    std::memcpy(kernelPtr(text,name.size()+1,true),name.c_str(),name.size()+1);return 0;
}
void postAsync(uint32_t hwnd,uint32_t message,uint32_t task,uint32_t error,uint32_t size){
    std::lock_guard<std::mutex> lock(messageMutex);windowMessages.push_back({hwnd,message,task,(error<<16)|(size&0xffff),messageTime()});
}
const std::set<std::string> winsockNames={"WSAStartup","WSACleanup","WSAGetLastError","WSAConnect","WSAEnumNetworkEvents","WSAEnumProtocolsA",
    "WSAEventSelect","WSARecv","WSARecvFrom","WSASend","WSASendTo","WSASocketA","WSAAsyncGetHostByAddr","WSAAsyncGetHostByName",
    "htonl","ntohl","htons","ntohs","inet_ntoa","inet_addr","setsockopt","getsockopt","bind","accept","listen","recv","recvfrom",
    "send","sendto","closesocket","getsockname"};
bool winsockDispatch(const std::string& name){
    if(!winsockNames.count(name))return false;
    if(netDebug&&name.compare(0,3,"hto")&&name.compare(0,3,"nto"))__android_log_print(ANDROID_LOG_INFO,"AMRTS","ws2 %s(%08x,%08x,%08x,%08x)",name.c_str(),arg(0),arg(1),arg(2),arg(3));
    if(name=="WSAStartup"){
        uint32_t data=arg(1);if(data){std::memset(kernelPtr(data,400,true),0,400);uint16_t versions[2]={0x0202,0x0202};std::memcpy(kernelPtr(data,4,true),versions,4);
            const char text[]="Android Winsock 2.2";std::memcpy(kernelPtr(data+4,sizeof text,true),text,sizeof text);
            const char status[]="Running";std::memcpy(kernelPtr(data+261,sizeof status,true),status,sizeof status);
            uint16_t limits[2]={1024,65467};std::memcpy(kernelPtr(data+390,4,true),limits,4);}
        ret(0,2);return true;
    }
    if(name=="WSACleanup"){ret(0,0);return true;}
    if(name=="WSAGetLastError"){ret(MEM32((g_fs_base?g_fs_base:kTibBase)+0x34),0);return true;}
    if(name=="htons"||name=="ntohs"){ret(uint16_t(__builtin_bswap16(uint16_t(arg(0)))),1);return true;}
    if(name=="htonl"||name=="ntohl"){ret(__builtin_bswap32(arg(0)),1);return true;}
    if(name=="inet_addr"){in_addr value{};ret(inet_aton(guestString(arg(0)).c_str(),&value)?value.s_addr:UINT32_MAX,1);return true;}
    if(name=="inet_ntoa"){
        in_addr value{};value.s_addr=arg(0);std::string text=inet_ntoa(value);if(!winsockStatic)winsockStatic=allocate(64);
        std::memcpy(kernelPtr(winsockStatic,text.size()+1,true),text.c_str(),text.size()+1);ret(winsockStatic,1);return true;
    }
    if(name=="WSAEnumProtocolsA"){
        uint32_t buffer=arg(1),lengthAddress=arg(2),need=2*372;
        if(!buffer||read32(lengthAddress)<need){write32(lengthAddress,need);ret(winsockFail(10055),3);return true;}
        writeProtocolInfo(buffer,1,6);writeProtocolInfo(buffer+372,2,17);ret(2,3);return true;
    }
    if(name=="WSASocketA"){
        int type=int(arg(1)),protocol=int(arg(2));
        if(arg(3)){type=int(read32(arg(3)+88));protocol=int(read32(arg(3)+92));}
        int fd=::socket(AF_INET,type==1?SOCK_STREAM:SOCK_DGRAM,protocol);
        if(fd<0){ret(winsockFail(wsaError(errno)),6);return true;}
        uint32_t handle=nextSocketHandle;nextSocketHandle+=4;GuestSocket socket;socket.fd=fd;socket.type=type==1?1:2;guestSockets[handle]=socket;
        if(netDebug)__android_log_print(ANDROID_LOG_INFO,"AMRTS","ws2 socket %08x type %d fd %d",handle,type,fd);
        ret(handle,6);return true;
    }
    uint32_t handle=arg(0);GuestSocket* socket=socketOf(handle);
    auto argc=[&](){
        static const std::unordered_map<std::string,uint32_t> counts={{"closesocket",1},{"bind",3},{"listen",2},{"accept",3},{"getsockname",3},
            {"getsockopt",5},{"setsockopt",5},{"send",4},{"recv",4},{"sendto",6},{"recvfrom",6},{"WSAConnect",7},{"WSAEventSelect",3},
            {"WSAEnumNetworkEvents",3},{"WSASend",7},{"WSARecv",7},{"WSASendTo",9},{"WSARecvFrom",9},
            {"WSAAsyncGetHostByName",5},{"WSAAsyncGetHostByAddr",7}};
        auto found=counts.find(name);return found==counts.end()?0u:found->second;};
    if(name=="WSAAsyncGetHostByName"||name=="WSAAsyncGetHostByAddr"){
        bool byName=name=="WSAAsyncGetHostByName";uint32_t hwnd=arg(0),message=arg(1),buffer=byName?arg(3):arg(5),size=byName?arg(4):arg(6);
        uint32_t task=winsockAsync++,error=0,address=0;std::string host;
        if(byName){host=guestString(arg(2));addrinfo hints{};hints.ai_family=AF_INET;addrinfo* result=nullptr;
            if(getaddrinfo(host.c_str(),nullptr,&hints,&result)==0&&result){address=reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr.s_addr;freeaddrinfo(result);}else error=11001;}
        else{std::memcpy(&address,kernelPtr(arg(2),4),4);in_addr value{};value.s_addr=address;host=inet_ntoa(value);}
        if(!error)error=writeHostEntry(buffer,size,host,address);
        postAsync(hwnd,message,task,error,size);ret(task,argc());return true;
    }
    if(!socket){ret(winsockFail(10038),argc());return true;}
    if(name=="closesocket"){::close(socket->fd);guestSockets.erase(handle);ret(0,1);return true;}
    if(name=="bind"){
        sockaddr_in address{};if(!guestAddress(arg(1),arg(2),address)){ret(winsockFail(10014),3);return true;}
        int bound=::bind(socket->fd,reinterpret_cast<sockaddr*>(&address),sizeof address);int error=errno;
        if(netDebug)__android_log_print(ANDROID_LOG_INFO,"AMRTS","ws2 bind %08x %s:%u -> %d errno %d",handle,inet_ntoa(address.sin_addr),ntohs(address.sin_port),bound,bound?error:0);
        ret(bound==0?0:winsockFail(wsaError(error)),3);return true;
    }
    if(name=="listen"){bool ok=::listen(socket->fd,int(arg(1)))==0;if(ok)socket->listening=true;ret(ok?0:winsockFail(wsaError(errno)),2);return true;}
    if(name=="accept"){
        socket->armed|=FD_ACCEPT_;
        sockaddr_in address{};socklen_t length=sizeof address;int fd=::accept(socket->fd,reinterpret_cast<sockaddr*>(&address),&length);
        if(fd<0){ret(winsockFail(wsaError(errno)),3);return true;}
        putAddress(arg(1),arg(2),address);
        // Accepted sockets inherit the listener's WSAEventSelect association.
        GuestSocket accepted;accepted.fd=fd;accepted.type=1;accepted.connected=true;accepted.event=socket->event;accepted.mask=socket->mask;
        if(accepted.mask){int flags=fcntl(fd,F_GETFL);fcntl(fd,F_SETFL,flags|O_NONBLOCK);}
        uint32_t newHandle=nextSocketHandle;nextSocketHandle+=4;guestSockets[newHandle]=accepted;ret(newHandle,3);return true;
    }
    if(name=="getsockname"){
        sockaddr_in address{};socklen_t length=sizeof address;
        if(::getsockname(socket->fd,reinterpret_cast<sockaddr*>(&address),&length)!=0){ret(winsockFail(wsaError(errno)),3);return true;}
        putAddress(arg(1),arg(2),address);ret(0,3);return true;
    }
    if(name=="setsockopt"||name=="getsockopt"){
        int level=0,option=0;int mapped=mapSocketOption(arg(1),arg(2),level,option);
        if(mapped==0){ret(0,5);return true;}
        // StyxNet treats any rejected option as fatal; accept unknown ones.
        if(mapped<0){__android_log_print(ANDROID_LOG_WARN,"AMRTS","ws2 %s ignored level %u option %#x",name.c_str(),arg(1),arg(2));
            if(name=="getsockopt"){write32(arg(3),0);if(arg(4))write32(arg(4),4);}ret(0,5);return true;}
        if(name=="setsockopt"){uint32_t size=arg(4);std::vector<uint8_t> value(std::max(size,4u));std::memcpy(value.data(),kernelPtr(arg(3),size),size);
            if(level==SOL_SOCKET&&(option==SO_RCVTIMEO||option==SO_SNDTIMEO)){uint32_t ms;std::memcpy(&ms,value.data(),4);timeval tv{time_t(ms/1000),suseconds_t((ms%1000)*1000)};
                ret(::setsockopt(socket->fd,level,option,&tv,sizeof tv)==0?0:winsockFail(wsaError(errno)),5);return true;}
            if(level==SOL_SOCKET&&option==SO_LINGER){uint16_t parts[2];std::memcpy(parts,value.data(),4);linger l{parts[0],parts[1]};
                ret(::setsockopt(socket->fd,level,option,&l,sizeof l)==0?0:winsockFail(wsaError(errno)),5);return true;}
            ret(::setsockopt(socket->fd,level,option,value.data(),socklen_t(size))==0?0:winsockFail(wsaError(errno)),5);return true;}
        int value=0;socklen_t size=sizeof value;
        if(::getsockopt(socket->fd,level,option,&value,&size)!=0){ret(winsockFail(wsaError(errno)),5);return true;}
        if(level==SOL_SOCKET&&option==SO_ERROR&&value)value=int(wsaError(value));
        if(level==SOL_SOCKET&&option==SO_TYPE)value=value==SOCK_STREAM?1:2;
        write32(arg(3),uint32_t(value));if(arg(4))write32(arg(4),4);ret(0,5);return true;
    }
    if(name=="WSAConnect"){
        sockaddr_in address{};if(!guestAddress(arg(1),arg(2),address)){ret(winsockFail(10014),7);return true;}
        int result=::connect(socket->fd,reinterpret_cast<sockaddr*>(&address),sizeof address);
        if(netDebug)__android_log_print(ANDROID_LOG_INFO,"AMRTS","ws2 connect %08x %s:%u -> %d errno %d",handle,inet_ntoa(address.sin_addr),ntohs(address.sin_port),result,result?errno:0);
        if(result==0){socket->connected=true;if(socket->mask)signalSocket(*socket,FD_CONNECT_|FD_WRITE_);ret(0,7);return true;}
        if(errno==EINPROGRESS){socket->connecting=true;ret(winsockFail(10035),7);return true;}
        ret(winsockFail(wsaError(errno)),7);return true;
    }
    if(name=="WSAEventSelect"){
        socket->event=arg(1);socket->mask=arg(2);socket->pending=0;socket->writeArmed=true;socket->armed=0x3f;
        int flags=fcntl(socket->fd,F_GETFL);fcntl(socket->fd,F_SETFL,flags|O_NONBLOCK);
        winsockPoll();ret(0,3);return true;
    }
    if(name=="WSAEnumNetworkEvents"){
        winsockPoll();uint32_t output=arg(2);
        if(netDebug)__android_log_print(ANDROID_LOG_INFO,"AMRTS","ws2 events %08x = %#x errors connect %d close %d",handle,socket->pending,socket->errors[4],socket->errors[5]);
        std::memset(kernelPtr(output,44,true),0,44);write32(output,socket->pending);
        for(int i=0;i<10;++i)write32(output+4+i*4,uint32_t(socket->errors[i]));
        socket->pending=0;std::fill(std::begin(socket->errors),std::end(socket->errors),0);
        if(arg(1)){auto found=syncHandles.find(arg(1));if(found!=syncHandles.end())found->second->signaled=false;}
        ret(0,3);return true;
    }
    bool to=name=="sendto"||name=="WSASendTo",from=name=="recvfrom"||name=="WSARecvFrom";
    if(name=="send"||name=="sendto"||name=="WSASend"||name=="WSASendTo"){
        bool wsa=name[0]=='W';std::vector<uint8_t> data;uint32_t flags,addressArg=0,lengthArg=0;
        if(wsa){data=gatherBuffers(arg(1),arg(2));flags=arg(4);if(to){addressArg=arg(5);lengthArg=arg(6);}}
        else{uint32_t length=arg(2);auto* bytes=static_cast<const uint8_t*>(kernelPtr(arg(1),length));data.assign(bytes,bytes+length);flags=arg(3);if(to){addressArg=arg(4);lengthArg=arg(5);}}
        int hostFlags=MSG_NOSIGNAL|MSG_DONTWAIT|((flags&4)?MSG_DONTROUTE:0)|((flags&1)?MSG_OOB:0);
        ssize_t sent;sockaddr_in address{};
        if(to&&guestAddress(addressArg,lengthArg,address))sent=::sendto(socket->fd,data.data(),data.size(),hostFlags,reinterpret_cast<sockaddr*>(&address),sizeof address);
        else sent=::send(socket->fd,data.data(),data.size(),hostFlags);
        uint32_t count=argc();
        if(sent<0){if(errno==EWOULDBLOCK){socket->writeArmed=true;socket->armed|=FD_WRITE_;}ret(winsockFail(wsaError(errno)),count);return true;}
        if(wsa){if(arg(3))write32(arg(3),uint32_t(sent));completeOverlapped(to?arg(7):arg(5),uint32_t(sent));ret(0,count);}
        else ret(uint32_t(sent),count);
        return true;
    }
    if(name=="recv"||name=="recvfrom"||name=="WSARecv"||name=="WSARecvFrom"){
        bool wsa=name[0]=='W';uint32_t capacity,flags,addressArg=0,lengthArg=0;
        if(wsa){capacity=0;for(uint32_t i=0;i<arg(2);++i)capacity+=read32(arg(1)+i*8);flags=arg(4)?read32(arg(4)):0;if(from){addressArg=arg(5);lengthArg=arg(6);}}
        else{capacity=arg(2);flags=arg(3);if(from){addressArg=arg(4);lengthArg=arg(5);}}
        socket->armed|=FD_READ_|FD_OOB_;
        std::vector<uint8_t> data(std::max(capacity,1u));sockaddr_in address{};socklen_t length=sizeof address;
        int hostFlags=MSG_DONTWAIT|((flags&2)?MSG_PEEK:0)|((flags&1)?MSG_OOB:0);
        ssize_t received=::recvfrom(socket->fd,data.data(),capacity,hostFlags,reinterpret_cast<sockaddr*>(&address),&length);
        uint32_t count=argc();
        if(received<0){ret(winsockFail(errno==EMSGSIZE?10040:wsaError(errno)),count);return true;}
        if(received==0&&socket->type==1)socket->closed=true;
        if(from)putAddress(addressArg,lengthArg,address);
        if(wsa){scatterBuffers(arg(1),arg(2),data.data(),uint32_t(received));if(arg(3))write32(arg(3),uint32_t(received));if(arg(4))write32(arg(4),0);
            completeOverlapped(from?arg(7):arg(5),uint32_t(received));ret(0,count);}
        else{std::memcpy(kernelPtr(arg(1),uint32_t(received),true),data.data(),uint32_t(received));ret(uint32_t(received),count);}
        // Level-triggered FD_READ: re-arm if more data is still queued.
        return true;
    }
    throw GuestStop("Winsock function pending: "+name);
}
