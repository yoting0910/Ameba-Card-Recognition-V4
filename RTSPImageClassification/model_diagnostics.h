#pragma once
extern "C" {
#include "model_classification.h"
#include "hal_cache.h"
}
#define CARD_GEOMETRY_YIELD() delay(1)
#include "card_geometry_board.h"
#include "input_color_policy.h"
static PaperBalanceResult paperBalanceResult;
static CardGeometry::Processor geometry;
static CardGeometry::Result geometryResult;
static uint8_t preparedInput[224*224*3],inputPixels[224*224*3];
static volatile bool captureInput=false,inputReady=false;
static volatile int geometryStage=0,outputCount=0;
static volatile uint32_t geometryMs=0,geometryFailures=0,geometryFrames=0;
static int inputWidth=0,inputHeight=0,inputChannels=0,cameraWidth=0,cameraHeight=0;
static nn_tensor_format_t inputFormat={},outputFormat={};
static float rawOutput[MODEL_CLASS_COUNT]={};
static nn_postprocess_t originalPostprocess;
static void invalidateRecognition();
static int diagnosticPreprocess(void *data,nn_data_param_t *params,void *tensor,nn_tensor_param_t *format){
 inputWidth=format->dim[0].size[0];inputHeight=format->dim[0].size[1];inputChannels=format->dim[0].size[2];inputFormat=format->format[0];
 cameraWidth=params->img.width;cameraHeight=params->img.height;
 bool packed=inputWidth==672&&inputHeight==224&&inputChannels==1;
 bool planar=inputWidth==224&&inputHeight==224&&inputChannels==3;
 if((!packed&&!planar)||inputFormat.buf_type!=2){geometryStage=-1;invalidateRecognition();captureInput=false;return PP_SKIP_INF;}
 uint32_t began=millis();CardGeometry::Result completed=geometry.process((const uint8_t*)data,cameraWidth,cameraHeight,preparedInput,true);
 taskENTER_CRITICAL();geometryResult=completed;geometryMs=millis()-began;geometryStage=completed.stage;geometryFrames++;taskEXIT_CRITICAL();
 if(geometryStage!=2){geometryFailures++;invalidateRecognition();captureInput=false;return PP_SKIP_INF;}
 paperBalanceResult=applyInputColorPolicy(preparedInput,224*224);
 uint8_t *target=(uint8_t*)((void**)tensor)[0];
 if(packed)memcpy(target,preparedInput,sizeof(preparedInput));
 else for(size_t p=0;p<224*224;p++)for(int c=0;c<3;c++)target[c*224*224+p]=preparedInput[p*3+c];
 dcache_clean_by_addr((uint32_t*)target,sizeof(preparedInput));
 if(captureInput){memcpy(inputPixels,preparedInput,sizeof(inputPixels));inputReady=true;captureInput=false;}
 return PP_USE_RESULT;
}
static int diagnosticPostprocess(void *tensor,nn_tensor_param_t *format,void *result){
 outputFormat=format->format[0];outputCount=format->dim[0].size[0];
 // SDK postprocess expects FP16. Guard the output before invoking it.
 if(outputCount!=MODEL_CLASS_COUNT||outputFormat.buf_type!=1){outputCount=0;geometryStage=-2;invalidateRecognition();return 0;}
 int count=originalPostprocess(tensor,format,result);
 classification_res_t *values=(classification_res_t*)result;
 for(int i=0;i<count;i++)if(values[i].clsid>=0&&values[i].clsid<MODEL_CLASS_COUNT)rawOutput[values[i].clsid]=values[i].prob;
 outputCount=count;return count;
}
static void installModelDiagnostics(){
 originalPostprocess=img_classification.postprocess;
 img_classification.preprocess=diagnosticPreprocess;img_classification.postprocess=diagnosticPostprocess;
}
static void diagnosticJson(char *body,size_t capacity){
 int n=snprintf(body,capacity,"{\"model\":\"card_frame_index_roi_v4_52cards\",\"preprocessing\":\"portable_board_geometry_v3_touchsplit\",\"processing_location\":\"AMB82-MINI\",\"input_shape\":[%d,%d,%d],\"input_buf_type\":%d,\"camera_size\":[%d,%d],\"output_count\":%d,\"output_buf_type\":%d,\"geometry_stage\":%d,\"geometry_ms\":%lu,\"geometry_frames\":%lu,\"geometry_failures\":%lu,\"corner_bounds\":[%d,%d,%d,%d],\"raw_output\":[",inputWidth,inputHeight,inputChannels,inputFormat.buf_type,cameraWidth,cameraHeight,outputCount,outputFormat.buf_type,geometryStage,(unsigned long)geometryMs,(unsigned long)geometryFrames,(unsigned long)geometryFailures,geometryResult.bounds[0],geometryResult.bounds[1],geometryResult.bounds[2],geometryResult.bounds[3]);
 if(n<0||(size_t)n>=capacity)return;
 for(int i=0;i<MODEL_CLASS_COUNT;i++){int added=snprintf(body+n,capacity-n,"%s%.6f",i?",":"",rawOutput[i]);if(added<0||(size_t)added>=capacity-n)return;n+=added;}
 snprintf(body+n,capacity-n,"],\"paper_balanced\":%s,\"paper_rgb\":[%d,%d,%d],\"paper_gain_q10\":[%d,%d,%d]}",paperBalanceResult.applied?"true":"false",paperBalanceResult.paper[0],paperBalanceResult.paper[1],paperBalanceResult.paper[2],paperBalanceResult.gainQ10[0],paperBalanceResult.gainQ10[1],paperBalanceResult.gainQ10[2]);
}
