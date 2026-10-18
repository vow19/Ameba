// 執行嵌入網頁的原始 JavaScript；模擬 DOM/時鐘/HTTP，不宣稱瀏覽器實測。
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync(require('path').join(__dirname,'../WebPage.h'),'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const nodes={};let clock=10000;let intervals=[];let revoked=[];
const context={
 document:{getElementById(id){return nodes[id]||(nodes[id]={style:{},textContent:'',src:''});}},
 performance:{now:()=>clock},Date,URL:{createObjectURL:()=> 'blob:test',revokeObjectURL:u=>revoked.push(u)},
 setTimeout,clearTimeout,setInterval:f=>intervals.push(f),AbortController,
 fetch:async()=>{throw Error('offline');},
 Image:class{set src(v){this.onload();}}
};
vm.createContext(context);vm.runInContext(source,context);
(async()=>{
 await new Promise(r=>setTimeout(r,10));
 vm.runInContext('render()',context);assert(nodes.state.textContent.includes('尚無有效'));assert(nodes.cover.style.display==='grid');
 vm.runInContext("data={state:'SAFE',events:2,persons:0,sequence:9,warning:false,led_on:false,truncated:false};lastStatus=performance.now();lastImage=performance.now();render()",context);
 assert.equal(nodes.state.textContent,'SAFE');assert.equal(nodes.cover.style.display,'none');
 clock+=3100;vm.runInContext('render()',context);assert(nodes.state.textContent!=='SAFE');
 vm.runInContext("lastStatus=performance.now();lastImage=0;render()",context);assert(nodes.state.textContent!=='SAFE');
 vm.runInContext("lastImage=performance.now();data.state='STALE';render()",context);assert.equal(nodes.cover.style.display,'grid');
 vm.runInContext("data.state='WARNING';data.warning=true;data.led_on=true;render()",context);assert.equal(nodes.state.textContent,'WARNING');
 nodes.toggle.onclick();assert.equal(nodes.toggle.textContent,'恢復影像');assert(nodes.state.textContent!=='SAFE');
 console.log('PASS: initial offline, fresh SAFE, expired status, expired image, STALE, WARNING, pause (7 scenarios)');
})().catch(e=>{console.error(e);process.exitCode=1;});
