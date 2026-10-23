#pragma once
#include "bounded_write.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/tcp.h"
#ifdef read
#undef read
#endif
#ifdef write
#undef write
#endif
#include <errno.h>
#include <string.h>
class BoundedHttpClient {
 int socketFd;
public:
 explicit BoundedHttpClient(int fd=-1):socketFd(fd){}
 BoundedHttpClient(const BoundedHttpClient&)=delete;
 BoundedHttpClient& operator=(const BoundedHttpClient&)=delete;
 BoundedHttpClient(BoundedHttpClient&&other):socketFd(other.socketFd){other.socketFd=-1;}
 ~BoundedHttpClient(){stop();}
 bool connected()const{return socketFd>=0;}
 void stop(){if(socketFd>=0){lwip_close(socketFd);socketFd=-1;}}
 int read(uint8_t *bytes,size_t length){
  if(socketFd<0)return -1;
  int n=lwip_recv(socketFd,bytes,length,MSG_DONTWAIT);
  if(n==0){stop();return -1;}
  if(n<0){if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)return 0;stop();return -1;}
  return n;
 }
 size_t write(const uint8_t *bytes,size_t length){
  if(socketFd<0)return 0;
  const size_t written=boundedWrite(bytes,length,[this](const uint8_t *p,size_t n)->int{
   int result=lwip_send(socketFd,p,n,MSG_DONTWAIT);
   if(result<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return -2;
   return result;
  },[](){delay(1);},[]()->uint32_t{return millis();},750);
  if(written!=length)stop();return written;
 }
 void print(const char *text){write((const uint8_t*)text,strlen(text));}
};
class BoundedHttpServer {
 int listener=-1;uint16_t port;
public:
 explicit BoundedHttpServer(uint16_t p):port(p){}
 void stop(){if(listener>=0){lwip_close(listener);listener=-1;}}
 void begin(){
  stop();listener=lwip_socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
  if(listener<0)return;int reuse=1;
  lwip_setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
  sockaddr_in address={};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_ANY);
  if(lwip_bind(listener,(sockaddr*)&address,sizeof(address))<0||lwip_listen(listener,4)<0||lwip_fcntl(listener,F_SETFL,O_NONBLOCK)<0){stop();}
 }
 BoundedHttpClient available(){
  if(listener<0)return BoundedHttpClient();
  int fd=lwip_accept(listener,NULL,NULL);if(fd<0)return BoundedHttpClient();
  int nodelay=1;lwip_setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&nodelay,sizeof(nodelay));
  // Nonblocking reads and writes are also requested explicitly on every call.
  if(lwip_fcntl(fd,F_SETFL,O_NONBLOCK)<0){lwip_close(fd);return BoundedHttpClient();}
  return BoundedHttpClient(fd);
 }
};
