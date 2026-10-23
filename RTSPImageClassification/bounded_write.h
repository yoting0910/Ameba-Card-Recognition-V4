#pragma once
#include <stdint.h>
#include <stddef.h>
// send returns >0 actual bytes, -2 retryable, or <=0 fatal. The production
// transport uses MSG_DONTWAIT, so a single send cannot bypass this deadline.
template<class Send,class Wait,class Now>
size_t boundedWrite(const uint8_t *data,size_t length,Send send,Wait wait,Now now,uint32_t budget){
 const uint32_t start=now();size_t sent=0;
 while(sent<length&&(uint32_t)(now()-start)<budget){
  int n=send(data+sent,length-sent);
  if(n>0){if((size_t)n>length-sent)break;sent+=(size_t)n;}
  else if(n==-2)wait();else break;
 }
 return sent;
}
