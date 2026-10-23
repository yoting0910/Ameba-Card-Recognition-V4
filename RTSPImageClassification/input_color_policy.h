#pragma once
#include "runtime_config.h"
#include "paper_balance.h"
inline PaperBalanceResult applyInputColorPolicy(uint8_t *rgb,size_t pixels){
#if ENABLE_PAPER_BALANCE
 return balancePaper(rgb,pixels);
#else
 (void)rgb;(void)pixels;
 return PaperBalanceResult();
#endif
}
