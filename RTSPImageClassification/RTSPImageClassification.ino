// Card outline -> rectification -> rank/small suit ROI -> 52-class inference.
// All vision runs on AMB82-MINI. The browser only displays output and controls preview.
#include <WiFi.h>
#include "VideoStream.h"
#include "StreamIO.h"
#include "NNImageClassification.h"
#include "ClassificationClassList.h"
static_assert(MODEL_CLASS_COUNT==52,"V4 requires 52 matching output labels");
#include "model_selection.h"
#if !CARD_INDEX_MODEL_READY
#error "Corner NB packaging is not verified yet. Do not use an older model."
#endif
#include "wifi_config.h"
#include <stdlib.h>
#include <string.h>
extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}
#include "bounded_http.h"
#include "model_diagnostics.h"
#include "viewer_page.h"
#define JPEG_CHANNEL 0
#define NN_CHANNEL 3
#define MAX_SNAPSHOT (1024UL*1024UL)
// Same field of view and dimensions as the original source JPEG captures.
VideoSetting previewConfig(960,540,5,VIDEO_JPEG,1);
VideoSetting nnConfig(960,544,5,VIDEO_RGB,0);
NNImageClassification imgclass;
StreamIO inferenceStream(1,1);
BoundedHttpServer server(80);
static uint8_t *snapshot;
// Single buffer ownership: worker writes only while busy; HTTP reads only ready.
static volatile bool snapshotBusy=false,snapshotReady=false;
static volatile uint32_t snapshotLength=0;
static TaskHandle_t snapshotTaskHandle=NULL;
static void requestSnapshot(){
 taskENTER_CRITICAL();
 if(snapshotTaskHandle&&!snapshotBusy&&!snapshotReady)snapshotBusy=true;
 taskEXIT_CRITICAL();
}
static void snapshotTask(void*){
 for(;;){
  if(!snapshotBusy){delay(10);continue;}
  uint32_t addr=0,len=0;Camera.getImage(JPEG_CHANNEL,&addr,&len);
  bool valid=addr&&len>=4&&len<=MAX_SNAPSHOT;
  if(valid)memcpy(snapshot,(void*)addr,len);
  taskENTER_CRITICAL();snapshotLength=valid?len:0;snapshotReady=valid;snapshotBusy=false;taskEXIT_CRITICAL();
 }
}
static volatile uint32_t resultPacked=65535,lastResultMs=0,frameCount=0,acceptedEvents=0;
static volatile uint32_t invalidGeneration=0;
static volatile float resultMargin=0;
static volatile int recognitionState=0; // 0 no target, 1 accepted, 2 confirming, 3 uncertain
static uint32_t networkCheckedMs=0,lastReconnectAttempt=0,knownNetworkIP=0;
static bool networkConnected=true;
// Initial conservative UI gate; live acceptance/coverage must be measured separately.
static const float MIN_PROBABILITY=.60f,MIN_MARGIN=.10f;
static void inferenceResult();
static void invalidateRecognition(){resultPacked=65535;invalidGeneration++;resultMargin=0;recognitionState=0;}
static bool networkReady(){
 uint32_t now=millis();if(now-networkCheckedMs<1000)return networkConnected;networkCheckedMs=now;
 if(WiFi.status()!=WL_CONNECTED){
  if(networkConnected){networkConnected=false;server.stop();invalidateRecognition();Serial.println("Wi-Fi lost. Waiting to reconnect...");}
  if(now-lastReconnectAttempt>=10000){
   // The Wi-Fi driver can wait during association; keep it below inference priority.
   vTaskPrioritySet(NULL,1);WiFi.begin(WIFI_SSID,WIFI_PASSWORD);vTaskPrioritySet(NULL,4);lastReconnectAttempt=millis();
  }
  if(WiFi.status()!=WL_CONNECTED)return false;
 }
 uint32_t ip=(uint32_t)WiFi.localIP();
 if(!networkConnected||knownNetworkIP!=ip){server.begin();invalidateRecognition();knownNetworkIP=ip;Serial.print("Open browser: http://");Serial.print(WiFi.localIP());Serial.println("/");}
 networkConnected=true;return true;
}
static void header(BoundedHttpClient &c,const char *type,size_t length,const char *frameResult=NULL){
 char h[256];int n=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %lu\r\n",type,(unsigned long)length);c.write((const uint8_t*)h,n);
 if(frameResult){c.print("X-Card-Result: ");c.print(frameResult);c.print("\r\n");}c.print("\r\n");
}
static void resultJson(char *body,size_t size,bool compact=false){
 // Snapshot only small fixed fields while the producer is paused. Never hold
 // a critical section while formatting JSON, writing sockets or copying JPEG.
 taskENTER_CRITICAL();
 uint32_t packed=resultPacked,frames=frameCount,last=lastResultMs,events=acceptedEvents,elapsed=geometryMs;
 int stage=geometryStage,state=recognitionState,w=cameraWidth,h=cameraHeight;float margin=resultMargin;CardGeometry::Result quad;if(!compact)quad=geometryResult;
 taskEXIT_CRITICAL();
 int accepted=packed&255,candidate=(packed>>8)&255;
 if(compact){snprintf(body,size,"{\"firmware_revision\":\"async_preview_v6_model_v4_52\",\"accepted\":%d,\"age_ms\":%lu,\"geometry_stage\":%d,\"recognition_state\":%d}",accepted==255?-1:accepted,(unsigned long)(frames?millis()-last:99999),stage,state);return;}
 snprintf(body,size,"{\"model\":\"card_frame_index_roi_v4_52cards\",\"firmware_revision\":\"async_preview_v6_model_v4_52\",\"accepted\":%d,\"candidate\":%d,\"score\":%.3f,\"margin\":%.3f,\"frames\":%lu,\"age_ms\":%lu,\"events\":%lu,\"geometry_stage\":%d,\"geometry_ms\":%lu,\"camera_size\":[%d,%d],\"quad\":[[%.1f,%.1f],[%.1f,%.1f],[%.1f,%.1f],[%.1f,%.1f]],\"minimum_probability\":%.2f,\"minimum_margin\":%.2f}",accepted==255?-1:accepted,candidate==255?-1:candidate,((packed>>16)&65535)/1000.0,margin,(unsigned long)frames,(unsigned long)(frames?millis()-last:99999),(unsigned long)events,stage,(unsigned long)elapsed,w,h,quad.points[0].x,quad.points[0].y,quad.points[1].x,quad.points[1].y,quad.points[2].x,quad.points[2].y,quad.points[3].x,quad.points[3].y,MIN_PROBABILITY,MIN_MARGIN);
}
static void unavailable(BoundedHttpClient &c){c.print("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");}
static void sendBytes(BoundedHttpClient &c,const uint8_t *data,size_t length){
 uint32_t start=millis();for(size_t sent=0;sent<length&&c.connected()&&millis()-start<5000;){size_t chunk=length-sent;if(chunk>8192)chunk=8192;size_t n=c.write(data+sent,chunk);if(!n)break;sent+=n;delay(1);}
}
void setup(){
 Serial.begin(115200);delay(1000);Serial.println("MODEL card_frame_index_roi_v4_52cards; firmware async_preview_v6_model_v4_52; automatic on-board geometry");
 while(WiFi.begin(WIFI_SSID,WIFI_PASSWORD)!=WL_CONNECTED){Serial.println("Connecting to configured Wi-Fi...");delay(3000);}
 snapshot=(uint8_t*)malloc(MAX_SNAPSHOT);if(!snapshot){Serial.println("Snapshot allocation failed");return;}
 Camera.configVideoChannel(JPEG_CHANNEL,previewConfig);Camera.configVideoChannel(NN_CHANNEL,nnConfig);Camera.videoInit();
 imgclass.configVideo(nnConfig);imgclass.configInputImageColor(1);imgclass.setResultCallback(inferenceResult);
    imgclass.modelSelect(IMAGE_CLASSIFICATION, NA_MODEL, NA_MODEL, NA_MODEL, NA_MODEL, CUSTOMIZED_IMGCLASS);
 installModelDiagnostics();imgclass.begin();
 inferenceStream.registerInput(Camera.getStream(NN_CHANNEL));inferenceStream.registerOutput(imgclass);
 inferenceStream.setStackSize();inferenceStream.setTaskPriority();
 if(inferenceStream.begin()!=0){Serial.println("Inference stream failed");return;}
 Camera.channelBegin(JPEG_CHANNEL);Camera.channelBegin(NN_CHANNEL);server.begin();
 // A stalled SDK snapshot must not stall HTTP/result handling or inference.
 if(xTaskCreate(snapshotTask,"card_snapshot",2048,NULL,2,&snapshotTaskHandle)!=pdPASS){snapshotTaskHandle=NULL;Serial.println("Preview task allocation failed; result API remains available");}
 requestSnapshot();
 knownNetworkIP=(uint32_t)WiFi.localIP();networkConnected=true;
 Serial.print("Open browser: http://");Serial.print(WiFi.localIP());Serial.println("/");
 // SDK Arduino main is priority 1; inference stream is priority 3. Let short
 // HTTP work preempt geometry, then block/yield when idle so inference runs.
 vTaskPrioritySet(NULL,4);
}
void loop(){
 if(!snapshot){delay(100);return;}if(!networkReady()){delay(20);return;}
 BoundedHttpClient c=server.available();if(!c.connected()){delay(5);return;}
 String request;request.reserve(256);uint32_t began=millis();
 while(c.connected()&&millis()-began<750&&request.length()<1024){
  uint8_t incoming[128];int n=c.read(incoming,sizeof(incoming));if(n<0)break;if(n==0){delay(1);continue;}
  for(int i=0;i<n&&request.length()<1024;i++)request+=(char)incoming[i];if(request.endsWith("\r\n\r\n"))break;
 }
 if(!request.endsWith("\r\n\r\n")){c.stop();delay(2);return;}
 if(request.startsWith("GET /nn-input")){
  inputReady=false;captureInput=true;uint32_t start=millis();while(captureInput&&millis()-start<2500)delay(1);captureInput=false;
  if(inputReady){const char *ppm="P6\n224 224\n255\n";size_t prefix=strlen(ppm);header(c,"image/x-portable-pixmap",prefix+sizeof(inputPixels));c.print(ppm);sendBytes(c,inputPixels,sizeof(inputPixels));}else unavailable(c);
 }else if(request.startsWith("GET /diagnostic")){
  char body[2048];diagnosticJson(body,sizeof(body));header(c,"application/json",strlen(body));c.print(body);
 }else if(request.startsWith("GET /result")){
  char body[1024];resultJson(body,sizeof(body));
  header(c,"application/json; charset=utf-8",strlen(body));c.print(body);
 }else if(request.startsWith("GET /photo")||request.startsWith("GET /frame")){
  if(snapshotReady){uint32_t len=snapshotLength;char body[1024];const bool combined=request.startsWith("GET /frame");if(combined)resultJson(body,sizeof(body),true);header(c,"image/jpeg",len,combined?body:NULL);sendBytes(c,snapshot,len);
   taskENTER_CRITICAL();snapshotReady=false;taskEXIT_CRITICAL();
  }else unavailable(c);
  requestSnapshot();
 }else if(request.startsWith("GET / ")){header(c,"text/html; charset=utf-8",strlen(VIEWER_PAGE));c.print(VIEWER_PAGE);}
 else c.print("HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
 c.stop();delay(2);
}
static void inferenceResult(){
 static int previous=-1,eventCard=-1;static uint32_t consecutive=0,lastGeneration=0;
 if(lastGeneration!=invalidGeneration){previous=-1;eventCard=-1;consecutive=0;lastGeneration=invalidGeneration;}
 if(geometryStage!=2||outputCount!=MODEL_CLASS_COUNT){invalidateRecognition();return;}
 int id=-1;float best=-1,runner=-1,sum=0;
 for(int i=0;i<MODEL_CLASS_COUNT;i++){float p=rawOutput[i];if(!std::isfinite(p)||p<0||p>1.01f){invalidateRecognition();return;}sum+=p;if(p>best){runner=best;best=p;id=i;}else if(p>runner)runner=p;}
 if(sum<.95f||sum>1.05f){invalidateRecognition();return;}
 resultMargin=best-runner;
 bool confident=best>=MIN_PROBABILITY&&best-runner>=MIN_MARGIN;
 consecutive=(confident&&id==previous)?consecutive+1:(confident?1:0);previous=confident?id:-1;
 int accepted=consecutive>=2?id:255;
 recognitionState=accepted!=255?1:(confident?2:3);
 if(accepted!=255&&eventCard!=id){acceptedEvents++;eventCard=id;}
 uint32_t milli=(uint32_t)(best*1000+.5f);if(milli>1000)milli=1000;
 resultPacked=((uint32_t)accepted)|((uint32_t)id<<8)|(milli<<16);lastResultMs=millis();frameCount++;
}
