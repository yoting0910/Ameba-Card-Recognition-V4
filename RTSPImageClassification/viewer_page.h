#pragma once
static const char VIEWER_PAGE[]=R"CARDPAGE(<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>撲克牌辨識</title><style>
*{box-sizing:border-box}body{font:18px system-ui;margin:24px;background:#f2f5f7;color:#20313e}main{max-width:1260px;margin:auto}h1{font-size:32px;margin:0 0 8px}.note{font-size:16px;color:#526a7a}.layout{display:grid;grid-template-columns:minmax(0,3fr) minmax(250px,1fr);gap:24px;background:white;padding:20px;border-radius:14px;align-items:start}img{width:100%;display:block;border-radius:7px}#result{font-size:34px;font-weight:600;white-space:pre-line;line-height:1.8;margin:8px 0 20px}button{font:inherit;background:#087c83;color:white;padding:10px 18px;border:0;border-radius:6px;cursor:pointer}@media(max-width:760px){body{margin:12px}.layout{grid-template-columns:1fr}#result{font-size:28px;margin:0 0 12px}h1{font-size:28px}}
</style><main><h1>撲克牌辨識</h1><p class="note">紅心、黑桃、方塊、梅花 A～K（52張）</p><section class="layout"><img id="photo" alt="等待攝影機影像"><div><div id="result" role="status">等待辨識</div><p id="status" class="note">將一張牌完整放入鏡頭。</p><button id="pause">暫停畫面</button></div></section></main><script>
let paused=false,generation=0,url=null,busy=false,activeController=null,failures=0,waitingSince=0;
const byId=id=>document.getElementById(id);
const labels=["10_club","10_diamond","10_heart","10_spade","2_club","2_diamond","2_heart","2_spade","3_club","3_diamond","3_heart","3_spade","4_club","4_diamond","4_heart","4_spade","5_club","5_diamond","5_heart","5_spade","6_club","6_diamond","6_heart","6_spade","7_club","7_diamond","7_heart","7_spade","8_club","8_diamond","8_heart","8_spade","9_club","9_diamond","9_heart","9_spade","A_club","A_diamond","A_heart","A_spade","J_club","J_diamond","J_heart","J_spade","K_club","K_diamond","K_heart","K_spade","Q_club","Q_diamond","Q_heart","Q_spade"];
const faceNumbers={A:1,J:11,Q:12,K:13};
byId('pause').onclick=()=>{paused=!paused;generation++;waitingSince=0;if(activeController)activeController.abort();byId('pause').textContent=paused?'恢復畫面':'暫停畫面';byId('status').textContent=paused?'畫面已暫停':'將一張牌完整放入鏡頭。';};
async function tick(){
 const g=generation,controller=new AbortController(),timer=setTimeout(()=>controller.abort(),7000);
 try{if(!paused&&!busy){
  busy=true;activeController=controller;
  const response=await fetch('/frame',{cache:'no-store',signal:controller.signal});
  if(!response.ok)throw Error(response.status===404?'請重新燒錄新版程式，再重新整理網頁':'影像讀取失敗');
  const metadata=response.headers.get('X-Card-Result');if(!metadata)throw Error('請重新整理網頁');
  const blob=await response.blob();
  if(paused||g!==generation)return;
  const next=URL.createObjectURL(blob),decoded=new Image();decoded.src=next;
  try{await decoded.decode();}catch(e){URL.revokeObjectURL(next);throw Error('影像讀取中，請稍候');}
  if(paused||g!==generation){URL.revokeObjectURL(next);return;}
  const old=url;url=next;byId('photo').src=url;if(old)URL.revokeObjectURL(old);failures=0;

 }}catch(e){if(!paused&&g===generation){failures++;byId('photo').alt='影像讀取中';}}
 finally{busy=false;activeController=null;clearTimeout(timer);setTimeout(tick,failures?Math.min(3000,400*failures):180);}
}tick();
// Results do not wait for JPEG capture, transfer or image decoding.
function renderResult(r){
  const valid=r.geometry_stage===2&&r.age_ms<2500&&Number.isInteger(r.accepted)&&r.accepted>=0&&r.accepted<labels.length;
  if(valid){
   waitingSince=0;
   const [rank,suit]=labels[r.accepted].split('_'),number=faceNumbers[rank];
   byId('result').textContent='數字：'+(number?number+'（'+rank+'）':rank)+'\n花色：'+({heart:'紅心',spade:'黑桃',diamond:'方塊',club:'梅花'}[suit]||'未知');
   byId('status').textContent='辨識完成';
  }else{
   if(!waitingSince)waitingSince=Date.now();
   if(r.geometry_stage===0){byId('result').textContent='未找到完整牌面';byId('status').textContent='請讓整張牌的四個角入鏡，邊緣留一點空白。';}
   else if(r.geometry_stage===1){byId('result').textContent='牌面不夠清楚';byId('status').textContent='請將牌放正，避開反光。';}
   else if(r.geometry_stage<0){byId('result').textContent='辨識暫時無法使用';byId('status').textContent='請重新啟動板子。';}
   else if(Date.now()-waitingSince>=4000){byId('result').textContent='目前無法確定牌面';byId('status').textContent='請調整牌的位置或光線後再試。';}
   else{byId('result').textContent='正在確認牌面';byId('status').textContent='請保持牌面清楚，四角完整入鏡。';}
  }
}
let resultController=null;
const previousPause=byId('pause').onclick;
byId('pause').onclick=()=>{previousPause();if(resultController)resultController.abort();};
async function resultTick(){
 const g=generation,controller=new AbortController();
 const timer=setTimeout(()=>controller.abort(),2000);
 try{
  if(!paused){
   resultController=controller;
   const response=await fetch('/result',{cache:'no-store',signal:controller.signal});
   if(!response.ok)throw Error('result unavailable');
   const r=await response.json();
   if(!paused&&g===generation)renderResult(r);
  }
 }catch(e){if(!paused&&g===generation){byId('result').textContent='等待連線';byId('status').textContent='正在重新連線';}}
 finally{clearTimeout(timer);resultController=null;setTimeout(resultTick,250);}
}resultTick();
</script></html>)CARDPAGE";
