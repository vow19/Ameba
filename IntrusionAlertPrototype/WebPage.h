#pragma once
// 自含網頁，無 CDN；核心判斷在板子上，瀏覽器只顯示資料。
static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>危險區域入侵提醒器</title><style>
:root{font-family:system-ui,sans-serif;color:#edf4fa;background:#101a28}body{max-width:1000px;margin:auto;padding:24px}h1{font-size:26px;margin-bottom:8px}p{color:#bdcbdc;line-height:1.6}.cards{display:flex;gap:12px;flex-wrap:wrap;margin:20px 0}.card{background:#1c2a3e;padding:16px;border-radius:12px;flex:1;min-width:150px}.value{display:block;font-size:28px;font-weight:700;margin-top:8px}#state{color:#ffd777}.frame{position:relative;background:#060c14;aspect-ratio:16/9;border:1px solid #42516a;border-radius:12px;overflow:hidden}img{width:100%;height:100%;object-fit:contain}#cover{position:absolute;inset:0;display:grid;place-items:center;background:#101a28e8;font-size:20px;text-align:center;padding:18px}button,a{background:#b8e3ff;color:#071c2e;border:0;border-radius:7px;padding:10px 16px;font:inherit;cursor:pointer}small{display:block;color:#bdcbdc;margin-top:16px}#detail{font-variant-numeric:tabular-nums}
</style><h1>危險區域入侵提醒器</h1><p>黃框為警戒區；人物框底部中心點進入區域並持續確認後，觸發警告。紅框表示已觸發。</p>
<div class="cards"><div class="card">區域狀態<strong class="value" id="state">連線中</strong></div><div class="card">本次開機事件數<strong class="value" id="events">—</strong></div><div class="card">目前人物數<strong class="value" id="persons">—</strong></div></div>
<div class="frame"><img id="image" alt="攝影機影像"><div id="cover">等待新影像與辨識資料</div></div>
<p id="detail">尚未收到資料</p><button id="toggle">暫停影像</button><small>這是課程原型。畫面採定時快照，與辨識結果可能有時間差；暫停網頁不會停止板子的偵測與 LED。計數不保存於斷電之後。</small>
<script>
let running=true,busy=false,lastImage=0,lastStatus=0,data=null,url=null;
const $=id=>document.getElementById(id);
async function request(path,format){const c=new AbortController(),t=setTimeout(()=>c.abort(),4000);try{const r=await fetch(path,{cache:'no-store',signal:c.signal});if(!r.ok)throw Error('HTTP '+r.status);return await (format==='json'?r.json():r.blob());}finally{clearTimeout(t);}}
function render(){const now=performance.now();let message='';if(!running)message='影像已暫停';else if(!data||now-lastStatus>3000)message='連線中斷／狀態過期';else if(data.state==='WAITING'||data.state==='STALE')message='辨識資料尚未就緒／已過期';else if(now-lastImage>4000)message='影像等待中／已過期';
$('cover').style.display=message?'grid':'none';$('cover').textContent=message;$('state').textContent=message?'尚無有效即時狀態':data.state;$('state').style.color=message?'#ffd777':data.state==='WARNING'?'#ff8b85':'#7ee8ae';
if(data){$('events').textContent=data.events;$('persons').textContent=message?'—':data.persons;$('detail').textContent='結果序號 '+data.sequence+' · LED '+(data.led_on?'亮':'滅')+(data.warning?' · 警告鎖存中':'')+(data.truncated?' · 顯示框數已截斷':'');}}
async function cycle(){if(busy||!running)return;busy=true;try{data=await request('/status.json','json');lastStatus=performance.now();render();const blob=await request('/snapshot.jpg?t='+Date.now(),'blob');const next=URL.createObjectURL(blob);await new Promise((resolve,reject)=>{const probe=new Image();probe.onload=resolve;probe.onerror=reject;probe.src=next;});$('image').src=next;if(url)URL.revokeObjectURL(url);url=next;lastImage=performance.now();}catch(e){$('detail').textContent='等待重連：'+e.message;}finally{busy=false;render();}}
$('toggle').onclick=()=>{running=!running;$('toggle').textContent=running?'暫停影像':'恢復影像';if(running){lastImage=lastStatus=0;cycle();}render();};setInterval(cycle,900);setInterval(render,250);cycle();
</script></html>)HTML";
