#pragma once
#include <stdint.h>
#include <stddef.h>
struct PaperBalanceResult { bool applied;int paper[3],gainQ10[3];PaperBalanceResult():applied(false){for(int i=0;i<3;i++){paper[i]=0;gainQ10[i]=1024;}} };
// Reference is the median paper RGB of the 260 TRAINING crops only.
// Existing 789 curated crops fall inside the unchanged band; no weights are fitted.
inline PaperBalanceResult balancePaper(uint8_t *rgb,size_t pixels){
 PaperBalanceResult result;int histogram[3][256]={};size_t count=0;
 for(size_t p=0;p<pixels;p+=4){
  const uint8_t *v=rgb+p*3;int low=v[0],high=v[0];for(int c=1;c<3;c++){if(v[c]<low)low=v[c];if(v[c]>high)high=v[c];}
  if(low<120||(high-low)*100>45*high)continue;
  for(int c=0;c<3;c++)histogram[c][v[c]]++;count++;
 }
 if(count<500){
  for(int c=0;c<3;c++)for(int v=0;v<256;v++)histogram[c][v]=0;
  count=0;for(size_t p=0;p<pixels;p+=4){for(int c=0;c<3;c++)histogram[c][rgb[p*3+c]]++;count++;}
 }
 if(!count)return result;
 for(int c=0;c<3;c++){size_t total=0;for(int v=0;v<256;v++){total+=histogram[c][v];if(total>count/2){result.paper[c]=v;break;}}}
 const float target[3]={194.f,193.f,202.f};const float referenceMean=(194.f+193.f+202.f)/3;
 float mean=(result.paper[0]+result.paper[1]+result.paper[2])/3.f;if(mean<1)mean=1;
 float cast=0;
 for(int c=0;c<3;c++){float difference=(result.paper[c]/mean)/(target[c]/referenceMean)-1;if(difference<0)difference=-difference;if(difference>cast)cast=difference;}
 float brightness=mean/referenceMean;
 if(cast<=.12f&&brightness>=.90f&&brightness<=1.10f)return result;
 result.applied=true;
 for(int c=0;c<3;c++){float gain=target[c]/(result.paper[c]>0?result.paper[c]:1);if(gain<.65f)gain=.65f;if(gain>1.45f)gain=1.45f;result.gainQ10[c]=(int)(gain*1024+.5f);}
 for(size_t p=0;p<pixels;p++)for(int c=0;c<3;c++){int v=(rgb[p*3+c]*result.gainQ10[c]+512)>>10;rgb[p*3+c]=(uint8_t)(v>255?255:v);}
 return result;
}
