#pragma once
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef abs
#undef abs
#endif
#ifdef round
#undef round
#endif
#ifndef CARD_GEOMETRY_YIELD
#define CARD_GEOMETRY_YIELD() ((void)0)
#endif
// Portable CPU geometry. No network, file I/O, OpenCV or model inference here.
// The board uses this same implementation that is replayed by the host harness.
#include <stdint.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#ifdef CARD_HOST_REPLAY
#include <cstdio>
static bool debugGeometry=false;
#endif
namespace CardGeometry {
struct Point { float x,y; Point(float a=0,float b=0):x(a),y(b){} };
struct Piece { int x,y,w,h,area; };
struct Result { int stage; Point points[4]; int bounds[4]; float area; Result():stage(0),area(0){std::memset(bounds,0,sizeof(bounds));} };
inline float cross(Point a,Point b,Point c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
inline float distance(Point a,Point b){float x=a.x-b.x,y=a.y-b.y;return std::sqrt(x*x+y*y);}
inline int clampi(int x,int lo,int hi){return std::max(lo,std::min(hi,x));}
// All callers provide nonnegative RGB/gray samples. Truncation after +0.5
// is identical to floor here and avoids a floating-point library call.
inline uint8_t byte(float x){return (uint8_t)clampi((int)(x+.5f),0,255);}
class Processor {
 std::vector<uint8_t> gray,blur,mask,temp,closed,visited,card,alternate;
 std::vector<int> queue;
 std::vector<int> suppressed;
 bool gradientsReady=false;
 int sw,sh;
 float sample(const uint8_t *src,int w,int h,float x,float y,int ch,bool planar){
  x=std::max(0.f,std::min((float)w-1,x));y=std::max(0.f,std::min((float)h-1,y));
  int x0=(int)x,y0=(int)y,x1=std::min(x0+1,w-1),y1=std::min(y0+1,h-1);
  float ax=x-x0,ay=y-y0;int p00=y0*w+x0,p01=y0*w+x1,p10=y1*w+x0,p11=y1*w+x1;
  if(planar){src+=ch*w*h;return (1-ay)*((1-ax)*src[p00]+ax*src[p01])+ay*((1-ax)*src[p10]+ax*src[p11]);}
  return (1-ay)*((1-ax)*src[3*p00+ch]+ax*src[3*p01+ch])+ay*((1-ax)*src[3*p10+ch]+ax*src[3*p11+ch]);
 }
 void smallGray(const uint8_t *src,int w,int h,bool planar){
  float scale=std::min(1.f,320.f/std::max(w,h));sw=(int)std::round(w*scale);sh=(int)std::round(h*scale);
  gray.resize(sw*sh);blur.resize(sw*sh);mask.resize(sw*sh);temp.resize(sw*sh);closed.resize(sw*sh);visited.resize(sw*sh);
  // Area averaging retains the small-scale contour, rather than an arbitrary decimation.
  for(int y=0;y<sh;y++)for(int x=0;x<sw;x++){
   float x0=x*(float)w/sw,x1=(x+1)*(float)w/sw,y0=y*(float)h/sh,y1=(y+1)*(float)h/sh;
   float sum=0,weight=0;
   for(int yy=(int)y0;yy<(int)std::ceil(y1);yy++)for(int xx=(int)x0;xx<(int)std::ceil(x1);xx++){
    float ww=(std::min(x1,(float)xx+1)-std::max(x0,(float)xx))*(std::min(y1,(float)yy+1)-std::max(y0,(float)yy));
    int p=clampi(yy,0,h-1)*w+clampi(xx,0,w-1);float r,g,b;
    if(planar){r=src[p];g=src[w*h+p];b=src[2*w*h+p];}else{r=src[p*3];g=src[p*3+1];b=src[p*3+2];}
    sum+=ww*(.299f*r+.587f*g+.114f*b);weight+=ww;
   }
   gray[y*sw+x]=byte(sum/weight);
  }
  for(int y=0;y<sh;y++)for(int x=0;x<sw;x++){
   int v=0;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
    int xx=x+dx,yy=y+dy;if(xx<0)xx=-xx;if(yy<0)yy=-yy;if(xx>=sw)xx=2*sw-xx-2;if(yy>=sh)yy=2*sh-yy-2;
    v+=gray[yy*sw+xx]*(dx==0?2:1)*(dy==0?2:1);
   }blur[y*sw+x]=(uint8_t)((v+8)/16);
  }
 }
 void closeMask(){
  // A 3x3 rectangular max/min is separable. Replicating boundary samples is
  // equivalent to the previous clipped neighborhood, including image edges.
  for(int y=0;y<sh;y++)for(int x=0;x<sw;x++){
   int p=y*sw+x;temp[p]=std::max(mask[y*sw+std::max(0,x-1)],std::max(mask[p],mask[y*sw+std::min(sw-1,x+1)]));
  }
  for(int y=0;y<sh;y++)for(int x=0;x<sw;x++){
   int p=y*sw+x;closed[p]=std::max(temp[std::max(0,y-1)*sw+x],std::max(temp[p],temp[std::min(sh-1,y+1)*sw+x]));
  }
  for(int y=0;y<sh;y++)for(int x=0;x<sw;x++){
   int p=y*sw+x;temp[p]=std::min(closed[y*sw+std::max(0,x-1)],std::min(closed[p],closed[y*sw+std::min(sw-1,x+1)]));
  }
  for(int y=0;y<sh;y++)for(int x=0;x<sw;x++){
   int p=y*sw+x;closed[p]=std::min(temp[std::max(0,y-1)*sw+x],std::min(temp[p],temp[std::min(sh-1,y+1)*sw+x]));
  }
 }
 std::vector<Point> hull(std::vector<Point> points){
  std::sort(points.begin(),points.end(),[](Point a,Point b){return a.x<b.x||(a.x==b.x&&a.y<b.y);});
  std::vector<Point> h;
  for(size_t i=0;i<points.size();i++){while(h.size()>1&&cross(h[h.size()-2],h.back(),points[i])<=0)h.pop_back();h.push_back(points[i]);}
  size_t lower=h.size();
  for(int i=(int)points.size()-2;i>=0;i--){while(h.size()>lower&&cross(h[h.size()-2],h.back(),points[i])<=0)h.pop_back();h.push_back(points[i]);}
  if(h.size()>1)h.pop_back();return h;
 }
 void simplify(const std::vector<Point>&p,int first,int last,float epsilon,std::vector<Point>&out){
  float best=0;int at=-1;float length=distance(p[first],p[last]);
  for(int i=first+1;i<last;i++){float d=length>0?std::fabs(cross(p[first],p[last],p[i]))/length:distance(p[first],p[i]);if(d>best){best=d;at=i;}}
  if(at>=0&&best>epsilon){simplify(p,first,at,epsilon,out);simplify(p,at,last,epsilon,out);}else out.push_back(p[first]);
 }
 void candidate(std::vector<Point>&edge,Result&best){
  if(edge.size()<12)return;
  std::vector<Point> h=hull(edge);if(h.size()<4)return;
  float perimeter=0;for(size_t i=0;i<h.size();i++)perimeter+=distance(h[i],h[(i+1)%h.size()]);
  // Closed RDP with an opposite point, preserving the winding of the hull.
  int split=1;float far=0;for(size_t i=1;i<h.size();i++){float d=distance(h[0],h[i]);if(d>far){far=d;split=(int)i;}}
  h.push_back(h[0]);std::vector<Point> q;simplify(h,0,split,.025f*perimeter,q);simplify(h,split,(int)h.size()-1,.025f*perimeter,q);
  // RDP's starting point may be in the middle of a nearly straight card edge.
  // Close the loop and remove that redundant point (OpenCV also cleans it).
  while(q.size()>4){float smallest=1e9f;int remove=-1;for(size_t i=0;i<q.size();i++){
   Point a=q[(i+q.size()-1)%q.size()],b=q[(i+1)%q.size()];float len=distance(a,b);
   float d=len>0?std::fabs(cross(a,b,q[i]))/len:distance(a,q[i]);
   if(d<smallest){smallest=d;remove=(int)i;}}
   if(remove<0||smallest>.025f*perimeter)break;q.erase(q.begin()+remove);
  }
#ifdef CARD_HOST_REPLAY
  if(debugGeometry&&perimeter>200){std::printf("hull %d quad %d:",(int)h.size(),(int)q.size());for(size_t i=0;i<q.size();i++)std::printf(" %.0f,%.0f",q[i].x,q[i].y);std::printf("\n");}
#endif
  if(q.size()!=4)return;
  float area=0,lo=1e9f,hi=0;for(int i=0;i<4;i++){
   if(q[i].x<2||q[i].y<2||q[i].x>sw-3||q[i].y>sh-3)return;
   area+=q[i].x*q[(i+1)%4].y-q[i].y*q[(i+1)%4].x;
   float d=distance(q[i],q[(i+1)%4]);lo=std::min(lo,d);hi=std::max(hi,d);
  }area=std::fabs(area)*.5f;
  if(area<=.014f*sw*sh||area>=.75f*sw*sh||area<=best.area||lo<18||lo/hi<.22f)return;
  // Require bright interior as in the desktop detector.
  float sum=0;int n=0;
  for(int y=0;y<sh;y+=2)for(int x=0;x<sw;x+=2){bool inside=true;for(int i=0;i<4;i++)if(cross(q[i],q[(i+1)%4],Point((float)x,(float)y))<0){inside=false;break;}if(inside){sum+=gray[y*sw+x];n++;}}
  if(!n||sum/n<115)return;
  int start=0;for(int i=1;i<4;i++)if(q[i].x+q[i].y<q[start].x+q[start].y)start=i;
  best.area=area;best.stage=1;for(int i=0;i<4;i++)best.points[i]=q[(start+i)%4];
 }
 void components(Result&best,bool foreground){
  std::fill(visited.begin(),visited.end(),0);
  for(int p=0;p<sw*sh;p++){
   if(visited[p]||((closed[p]!=0)!=foreground))continue;
   queue.clear();queue.push_back(p);bool border=false;int area=0;std::vector<Point> edge;
   for(size_t j=0;j<queue.size();j++){
    int at=queue[j];if(visited[at])continue;int x=at%sw,y=at/sw,left=x,right=x;
    while(left>0&&!visited[y*sw+left-1]&&((closed[y*sw+left-1]!=0)==foreground))left--;
    while(right<sw-1&&!visited[y*sw+right+1]&&((closed[y*sw+right+1]!=0)==foreground))right++;
    for(int xx=left;xx<=right;xx++)visited[y*sw+xx]=1;
    area+=right-left+1;if(left==0||right==sw-1||y==0||y==sh-1)border=true;
    // The convex hull of every span's endpoints is exactly the hull of all
    // component pixels; collecting every boundary pixel is unnecessary.
    edge.push_back(Point((float)left,(float)y));edge.push_back(Point((float)right,(float)y));
    for(int dy=-1;dy<=1;dy+=2){int yy=y+dy;if(yy<0||yy>=sh)continue;
     bool inRun=false;for(int xx=std::max(0,left-1);xx<=std::min(sw-1,right+1);xx++){
      int next=yy*sw+xx;bool match=!visited[next]&&((closed[next]!=0)==foreground);
      if(match&&!inRun)queue.push_back(next);inRun=match;
     }
    }
   }
   if(!border&&area>40)candidate(edge,best);
  }
 }
 void edgeMask(int threshold){
  // Sobel/nonmaximum suppression/hysteresis; desktop uses OpenCV Canny.
  // The three threshold passes share exactly the same Sobel/NMS result.
  if(!gradientsReady){
  std::vector<int> gx(sw*sh),gy(sw*sh),mag(sw*sh);suppressed.assign(sw*sh,0);
  for(int y=1;y<sh-1;y++)for(int x=1;x<sw-1;x++){
   int p=y*sw+x;
   gx[p]=-blur[p-sw-1]+blur[p-sw+1]-2*blur[p-1]+2*blur[p+1]-blur[p+sw-1]+blur[p+sw+1];
   gy[p]=-blur[p-sw-1]-2*blur[p-sw]-blur[p-sw+1]+blur[p+sw-1]+2*blur[p+sw]+blur[p+sw+1];
   mag[p]=std::abs(gx[p])+std::abs(gy[p]);
  }
  for(int y=1;y<sh-1;y++)for(int x=1;x<sw-1;x++){
   int p=y*sw+x,ax=std::abs(gx[p]),ay=std::abs(gy[p]),a,b;
   if(ay*1000<ax*414){a=p-1;b=p+1;}else if(ay*414>ax*1000){a=p-sw;b=p+sw;}else if((gx[p]>0)==(gy[p]>0)){a=p-sw-1;b=p+sw+1;}else{a=p-sw+1;b=p+sw-1;}
   if(mag[p]>=mag[a]&&mag[p]>mag[b])suppressed[p]=mag[p];
  }
  gradientsReady=true;
  }
  std::fill(mask.begin(),mask.end(),0);queue.clear();
  for(int y=1;y<sh-1;y++)for(int x=1;x<sw-1;x++){
   int p=y*sw+x;if(suppressed[p]>threshold/3){mask[p]=128;if(suppressed[p]>threshold){mask[p]=255;queue.push_back(p);}}
  }
  for(size_t j=0;j<queue.size();j++){int p=queue[j],x=p%sw,y=p/sw;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
   int xx=x+dx,yy=y+dy;if(xx<0||yy<0||xx>=sw||yy>=sh)continue;int n=yy*sw+xx;if(mask[n]==128){mask[n]=255;queue.push_back(n);}
  }}
  for(size_t i=0;i<mask.size();i++)if(mask[i]!=255)mask[i]=0;
 }
 bool homography(Point *q,float *a){
  float m[8][9]={};float dst[4][2]={{0,0},{255,0},{255,383},{0,383}};
  for(int i=0;i<4;i++){float x=dst[i][0],y=dst[i][1],u=q[i].x,v=q[i].y;float r[9]={x,y,1,0,0,0,-u*x,-u*y,u};float s[9]={0,0,0,x,y,1,-v*x,-v*y,v};std::memcpy(m[2*i],r,sizeof(r));std::memcpy(m[2*i+1],s,sizeof(s));}
  for(int k=0;k<8;k++){
   int pivot=k;for(int j=k+1;j<8;j++)if(std::fabs(m[j][k])>std::fabs(m[pivot][k]))pivot=j;
   if(std::fabs(m[pivot][k])<1e-7f)return false;
   for(int j=k;j<9;j++)std::swap(m[k][j],m[pivot][j]);float v=m[k][k];for(int j=k;j<9;j++)m[k][j]/=v;
   for(int i=0;i<8;i++)if(i!=k){float f=m[i][k];for(int j=k;j<9;j++)m[i][j]-=f*m[k][j];}
  }for(int k=0;k<8;k++)a[k]=m[k][8];return true;
 }
 bool warp(const uint8_t *src,int w,int h,bool planar,Point *q,std::vector<uint8_t>&out){
  float a[8];if(!homography(q,a))return false;out.resize(256*384*3);
  const int pixelStep=planar?1:3,channelStep=planar?w*h:1;
  for(int y=0;y<384;y++)for(int x=0;x<256;x++){
   if(x==0&&y%32==0)CARD_GEOMETRY_YIELD();
   float d=a[6]*x+a[7]*y+1;if(std::fabs(d)<1e-6f)return false;
   float xx=(a[0]*x+a[1]*y+a[2])/d,yy=(a[3]*x+a[4]*y+a[5])/d;
   // OpenCV warpPerspective's default INTER_LINEAR uses 1/32 pixel weights.
   xx=std::max(0.f,std::min((float)w-1,xx));yy=std::max(0.f,std::min((float)h-1,yy));
   int x32=(int)(xx*32+.5f),y32=(int)(yy*32+.5f),x0=x32>>5,y0=y32>>5,fx=x32&31,fy=y32&31;
   int x1=std::min(x0+1,w-1),y1=std::min(y0+1,h-1);
   int p00=(y0*w+x0)*pixelStep,p01=(y0*w+x1)*pixelStep,p10=(y1*w+x0)*pixelStep,p11=(y1*w+x1)*pixelStep;
   int w00=(32-fx)*(32-fy),w01=fx*(32-fy),w10=(32-fx)*fy,w11=fx*fy;
   for(int c=0;c<3;c++){int offset=c*channelStep;out[(y*256+x)*3+c]=(uint8_t)((src[p00+offset]*w00+src[p01+offset]*w01+src[p10+offset]*w10+src[p11+offset]*w11+512)>>10);}
  }return true;
 }
 int luminance(const uint8_t *rgb,int p){return (77*rgb[p*3]+150*rgb[p*3+1]+29*rgb[p*3+2]+128)>>8;}
 float orientationScore(const std::vector<uint8_t>&rgb){
  int histogram[256]={};for(int p=0;p<256*384;p++)histogram[luminance(rgb.data(),p)]++;
  int total=0,level=0;for(;level<255;level++){total+=histogram[level];if(total>=(int)(256*384*.70f))break;}
  int threshold=std::min(170,level-25);float score=0;
  for(int y=6;y<130;y++)for(int x=6;x<58;x++){
   score+=(luminance(rgb.data(),y*256+x)<threshold)+(luminance(rgb.data(),(383-y)*256+255-x)<threshold)
         -(luminance(rgb.data(),y*256+255-x)<threshold)-(luminance(rgb.data(),(383-y)*256+x)<threshold);
  }return score;
 }
 // Same restricted ink-valley recovery as desktop _split_touching_suit.
 // Only runs after the ordinary small-suit search failed; RGB stays unchanged.
 bool splitTouchingSuit(const std::vector<uint8_t>&image,int threshold,const std::vector<Piece>&rank,int left,int right,int bottom,Piece&chosen){
  const int w=76,h=124;
  if(rank.size()!=2)return false;
  Piece a=rank[0],b=rank[1];if(a.x>b.x)std::swap(a,b);
  if(!(a.w<=.7*b.w&&std::min(a.h,b.h)>=.65*std::max(a.h,b.h)))return false;
  int y0=bottom+2,y1=std::min(h,bottom+44),lo=std::max(12,left+14),hi=std::min(52,right+6);
  if(y1-y0<15||hi-lo<8)return false;
  int ink[w]={};for(int y=y0;y<y1;y++)for(int x=0;x<w;x++)if(image[y*w+x]<threshold)ink[x]++;
  int cut=lo;double best=1000000;
  // Match NumPy's left-to-right float64 convolution, including nearly tied minima.
  for(int x=lo;x<hi;x++){double smooth=(ink[x-1]*(1.0/3.0)+ink[x]*(1.0/3.0))+ink[x+1]*(1.0/3.0);if(smooth<best){best=smooth;cut=x;}}
  int lp=0,rp=0;
  for(int x=std::max(0,left-6);x<std::max(left+1,cut-4);x++)lp=std::max(lp,ink[x]);
  for(int x=cut+4;x<std::min(w,hi+8);x++)rp=std::max(rp,ink[x]);
  if(std::min(lp,rp)<10||best>.5*std::min(lp,rp))return false;
  std::vector<uint8_t> seen(w*h,0);bool found=false;
  for(int y=y0;y<y1;y++)for(int x=0;x<cut;x++){
   int p=y*w+x;if(seen[p]||image[p]>=threshold)continue;
   queue.clear();queue.push_back(p);seen[p]=1;int x0=x,x1=x,yy0=y,yy1=y,bridge=0;
   for(size_t j=0;j<queue.size();j++){
    int at=queue[j],xx=at%w,yy=at/w;x0=std::min(x0,xx);x1=std::max(x1,xx);yy0=std::min(yy0,yy);yy1=std::max(yy1,yy);
    if(xx==cut-1)bridge++;
    for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
     int nx=xx+dx,ny=yy+dy;if(nx<0||nx>=cut||ny<y0||ny>=y1)continue;int z=ny*w+nx;
     if(!seen[z]&&image[z]<threshold){seen[z]=1;queue.push_back(z);}
    }
   }
   Piece s={x0,yy0,x1-x0+1,yy1-yy0+1,(int)queue.size()};
   if(s.x<=left+10&&s.y>=bottom-3&&s.y<=bottom+30&&s.w>=7&&s.w<=42&&s.h>=8&&s.h<=48&&
      s.w>=.4*s.h&&s.w<=1.6*s.h&&s.area>=40&&s.y+s.h<y1&&bridge<=.4*s.h){
    if(!found||s.y<chosen.y){chosen=s;found=true;}
   }
  }
  return found;
 }
 bool indexBounds(int *bounds){
  const int w=76,h=124;int hist[256]={};std::vector<uint8_t> image(w*h),seen(w*h,0);
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){int v=luminance(card.data(),y*256+x);image[y*w+x]=(uint8_t)v;hist[v]++;}
  int n=0,paper=0;for(;paper<255;paper++){n+=hist[paper];if(n>=(int)(w*h*.78f))break;}int threshold=std::min(175,paper-28);
  std::vector<Piece> pieces;
  for(int p=0;p<w*h;p++){
   if(seen[p]||image[p]>=threshold)continue;queue.clear();queue.push_back(p);seen[p]=1;int x0=p%w,x1=x0,y0=p/w,y1=y0;
   for(size_t j=0;j<queue.size();j++){int at=queue[j],x=at%w,y=at/w;x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);
    for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int xx=x+dx,yy=y+dy;if(xx<0||xx>=w||yy<0||yy>=h)continue;int z=yy*w+xx;if(!seen[z]&&image[z]<threshold){seen[z]=1;queue.push_back(z);}}
   }
   Piece v={x0,y0,x1-x0+1,y1-y0+1,(int)queue.size()};if(v.area>=8&&v.w>=2&&v.h>=4)pieces.push_back(v);
  }
  int anchor=-1;
  for(size_t i=0;i<pieces.size();i++){Piece p=pieces[i];if(p.x<=28&&p.y<48&&p.h>=10&&p.h<=64&&p.w<=54&&p.y+p.h<=80)
   if(anchor<0||p.h>pieces[anchor].h||(p.h==pieces[anchor].h&&p.area>pieces[anchor].area))anchor=(int)i;}
  if(anchor<0)return false;Piece a=pieces[anchor];int left=w,right=0,top=h,bottom=0;std::vector<Piece> rank;
  for(size_t i=0;i<pieces.size();i++){Piece p=pieces[i];if(p.x+p.w<=64&&p.w<=54&&p.h>=8&&p.h<=64&&p.y+p.h<=80&&std::min(p.y+p.h,a.y+a.h)-std::max(p.y,a.y)>=.45f*std::min(p.h,a.h)){
   rank.push_back(p);left=std::min(left,p.x);right=std::max(right,p.x+p.w);top=std::min(top,p.y);bottom=std::max(bottom,p.y+p.h);}}
  int suit=-1;
  for(size_t i=0;i<pieces.size();i++){Piece p=pieces[i];if(p.x<=std::max(28,left+12)&&p.y>=bottom-3&&p.y<=bottom+42&&p.w>=7&&p.w<=42&&p.h>=8&&p.h<=48&&p.x+p.w<=std::max(52,right+5))
   if(suit<0||p.y<pieces[suit].y)suit=(int)i;}
  Piece s;if(suit>=0)s=pieces[suit];else if(!splitTouchingSuit(image,threshold,rank,left,right,bottom,s))return false;
  bounds[0]=std::max(0,std::min(left,s.x)-4);bounds[1]=std::max(0,top-4);
  bounds[2]=std::min(256,std::max(right,s.x+s.w)+5);bounds[3]=std::min(384,std::max(bottom,s.y+s.h)+5);
  return bounds[2]-bounds[0]>=14&&bounds[3]-bounds[1]>=30;
 }
public:
#ifdef CARD_HOST_REPLAY
 bool replayIndexBounds(const uint8_t *rgb,int *bounds){card.assign(rgb,rgb+256*384*3);return indexBounds(bounds);}
 bool replaySplitMask(const uint8_t *mask,const int *stats,int count,int left,int right,int bottom,int *out){
  std::vector<uint8_t> image(76*124);for(int i=0;i<76*124;i++)image[i]=mask[i]?0:255;
  std::vector<Piece> rank;for(int i=0;i<count;i++){Piece p={stats[5*i],stats[5*i+1],stats[5*i+2],stats[5*i+3],stats[5*i+4]};rank.push_back(p);}
  Piece s;if(!splitTouchingSuit(image,128,rank,left,right,bottom,s))return false;
  out[0]=s.x;out[1]=s.y;out[2]=s.w;out[3]=s.h;out[4]=s.area;return true;
 }
 std::vector<uint8_t> replayMasks;
 int smallWidth()const{return sw;} int smallHeight()const{return sh;}
#endif
 const uint8_t *cardPixels()const{return card.empty()?0:card.data();}
 Result process(const uint8_t *src,int w,int h,uint8_t *input,bool planar=true){
  Result result;if(!src||!input||w<80||h<80||w>1920||h>1080)return result;smallGray(src,w,h,planar);gradientsReady=false;
#ifdef CARD_HOST_REPLAY
  replayMasks.clear();
#endif
  for(int pass=0;pass<7;pass++){
   if(pass<3)edgeMask(pass==0?60:(pass==1?100:160));else{int level=150+(pass-3)*20;for(size_t i=0;i<mask.size();i++)mask[i]=blur[i]>level?255:0;}
   closeMask();
#ifdef CARD_HOST_REPLAY
   replayMasks.insert(replayMasks.end(),closed.begin(),closed.end());
#endif
   components(result,true);components(result,false);CARD_GEOMETRY_YIELD();
  }
  if(!result.stage)return result;
  Point center;for(int i=0;i<4;i++){result.points[i].x*=w/(float)sw;result.points[i].y*=h/(float)sh;center.x+=result.points[i].x*.25f;center.y+=result.points[i].y*.25f;}
  for(int i=0;i<4;i++){result.points[i].x=std::max(0.f,std::min((float)w-1,center.x+(result.points[i].x-center.x)*1.02f));result.points[i].y=std::max(0.f,std::min((float)h-1,center.y+(result.points[i].y-center.y)*1.02f));}
  if(!warp(src,w,h,planar,result.points,card))return Result();
  Point rotated[4]={result.points[3],result.points[0],result.points[1],result.points[2]};
  if(warp(src,w,h,planar,rotated,alternate)&&orientationScore(alternate)>orientationScore(card)){card.swap(alternate);for(int i=0;i<4;i++)result.points[i]=rotated[i];}
  if(!indexBounds(result.bounds))return result;
  result.stage=2;int cw=result.bounds[2]-result.bounds[0],ch=result.bounds[3]-result.bounds[1];
  for(int y=0;y<224;y++)for(int x=0;x<224;x++){
   float xx=result.bounds[0]+std::max(0.f,std::min((float)cw-1,(x+.5f)*cw/224-.5f));
   float yy=result.bounds[1]+std::max(0.f,std::min((float)ch-1,(y+.5f)*ch/224-.5f));
   int x0=(int)xx,y0=(int)yy,x1=std::min(x0+1,255),y1=std::min(y0+1,383);float ax=xx-x0,ay=yy-y0;
   int p00=(y0*256+x0)*3,p01=(y0*256+x1)*3,p10=(y1*256+x0)*3,p11=(y1*256+x1)*3;
   for(int c=0;c<3;c++)input[(y*224+x)*3+c]=byte((1-ay)*((1-ax)*card[p00+c]+ax*card[p01+c])+ay*((1-ax)*card[p10+c]+ax*card[p11+c]));
  }return result;
 }
};
}
