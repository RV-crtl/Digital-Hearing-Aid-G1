/* G1 custom PCB/R26 candidate architecture. Never confuse live on-device ML with the UI demo. */
"use strict";
const $=id=>document.getElementById(id);
const paths={
  mic:{title:"PDM microphone · R24/R26 custom PCB",kind:"Reported working · R26 candidate",detail:"PDM microphone PC2/PB14 → DFSDM1 decimation → DMA PCM blocks → AudioTask ambient AGC and limiter → SAI2B speaker. R24 operation was reported; R26 SPL and distant pickup remain to be measured.",stages:["PDM microphone","DFSDM1 + DMA","PCM capture ring","AudioTask ambient AGC","Bounded output FIFO","SAI2B TX DMA","Speaker"]},
  bt:{title:"Bluetooth audio · existing source path",kind:"Firmware transport",detail:"Bluetooth receiver → SAI1A serial audio → circular RX DMA → completed-half snapshot → AudioTask task notification → output mono FIFO → SAI2B TX DMA.",stages:["BT module / serial audio","SAI1A RX / 24-bit","Circular DMA","Ring snapshot / AudioTask","DSP + sample FIFO","SAI2B TX DMA","Output transducer"]},
  tc:{title:"Telecoil · LOW16 verified in R22–R24",kind:"Operator-confirmed PCM decode",detail:"Induction loop → telecoil front end → PCM1808 → SAI2A 24-bit frames decoded as signed LOW16 → adaptive TC cleanup / FIFO → SAI2B. FILTER ON reduced audible static; remaining hiss needs physical measurement.",stages:["Induction-loop input","PCM1808 / SAI2A","LOW16 decode + DMA","AudioTask TC cleanup","Gain + output FIFO","SAI2B TX DMA","Speaker"]}
};
const tasks=[
  {key:"irq",title:"Peripheral DMA ISR",priority:"ISR · ceiling-aware",detail:"DMA half/full callback publishes a completed snapshot and uses vTaskNotifyGiveFromISR only at a FreeRTOS-compatible interrupt priority. ISR must not run complex DSP.",code:"Core/Src/demo_app.c"},
  {key:"audio",title:"AudioTask",priority:"FreeRTOS priority 7",detail:"Receives ISR notifications, consumes completed PCM blocks, owns processing state and the group event queue; uses a bounded 5-ms wait when idle.",code:"Core/Src/demo_app.c"},
  {key:"bt",title:"BtTask",priority:"FreeRTOS priority 5",detail:"Services Bluetooth UART and control/profile requests off the real-time audio path.",code:"Core/Src/demo_app.c"},
  {key:"control",title:"ControlTask / bootstrap",priority:"Control-loop context",detail:"Polls user input and diagnostic states; submits events to the AudioTask-owned queue. Custom PCB uses MPR121 I2C E8–E11 and PA0/PA1 encoder; legacy Nucleo B1 gestures are not product controls.",code:"Core/Src/demo_app.c"},
  {key:"guard",title:"DSP overload guardian",priority:"Within audio processing",detail:"Examines audio pipeline fault counters; bypasses optional DSP when necessary without claiming the underlying audio transfer is fail-safe.",code:"Core/Src/group_guardian.c"}
];
const tabs=document.querySelectorAll("[data-atlas-source]");
function renderFlow(key){const c=paths[key],heading=$("atlasFlowTitle"),detail=$("atlasFlowDesc"),steps=$("atlasFlowSteps");
  if(!c||!steps)return;
  heading.textContent=c.title;detail.textContent=c.detail;
  $("atlasFlowState").textContent=c.kind;
  steps.replaceChildren();
  c.stages.forEach((stage,i)=>{const el=document.createElement("button");el.type="button";el.className="atlas-node";el.innerHTML="<small>"+String(i+1).padStart(2,"0")+"</small><strong></strong><span>"+(i<2?"SOURCE":i===2?"BUFFER":i===3?"TASK":i===4?"DSP / FIFO":i===5?"TX DMA":"PHYSICAL")+"</span>";el.querySelector("strong").textContent=stage;el.title="Stage "+(i+1)+": "+stage;el.addEventListener("click",()=>{document.querySelectorAll(".atlas-node").forEach(x=>x.classList.remove("active"));el.classList.add("active");$("atlasInspect").textContent=stage+" — "+(i===2?"The DMA ring transfers samples independently of CPU instruction timing; a completed half must be copied or consumed before overwrite.":i===3?"FreeRTOS synchronization and ownership boundaries protect sample integrity.":i===5?"Output DMA feeds the audio serial peripheral while processing continues.":"Inspect the associated source files and original KiCad schematic for electrical implementation.");});steps.append(el);});
}
tabs.forEach(el=>el.addEventListener("click",()=>{tabs.forEach(x=>x.setAttribute("aria-pressed",String(x===el)));renderFlow(el.dataset.atlasSource);}));
const taskWrap=$("atlasTasks");if(taskWrap){tasks.forEach(t=>{const b=document.createElement("button");b.type="button";b.className="atlas-task";b.innerHTML="<span class='atlas-task-priority'></span><strong></strong><small></small>";b.querySelector("span").textContent=t.priority;b.querySelector("strong").textContent=t.title;b.querySelector("small").textContent=t.detail;b.addEventListener("click",()=>{document.querySelectorAll(".atlas-task").forEach(n=>n.classList.remove("active"));b.classList.add("active");$("atlasTaskInspect").textContent=t.detail;$("atlasTaskSource").href="https://github.com/RV-crtl/Digital-Hearing-Aid-G1/blob/main/"+t.code;$("atlasTaskSource").textContent="View owning source ↗";});taskWrap.append(b);});}
renderFlow("bt");
const gain=$("atlasLearningGain"),level=$("atlasLearningLevel"),action=$("atlasLearningAction"),indicator=$("atlasLearningResult");
let weights=new Float32Array(3),counts=new Uint8Array(3);
const levels=["quiet","moderate","loud"];
function step(){const k=Number(level.value);$("atlasLearningScene").textContent=levels[k];$("atlasLearningGain").textContent=(weights[k]>=0?"+":"")+weights[k].toFixed(1)+" dB";indicator.textContent="Browser-only preference simulation in "+levels[k]+" scene; not live R26 telemetry.";}
level?.addEventListener("input",step);
$("atlasLearnButton")?.addEventListener("click",()=>{const k=Number(level.value),userAction=action.value==="up"?1:-1;counts[k]++;const rate=1/Math.sqrt(counts[k]);weights[k]=Math.max(-6,Math.min(3,weights[k]+userAction*0.8*rate));step();});
$("atlasLearningReset")?.addEventListener("click",()=>{weights.fill(0);counts.fill(0);step();});
if(level)step();

$("atlasPrint")?.addEventListener("click",()=>{
  // On the hosted Pages release open the versioned A3 SVG; repository previews
  // without generated plots use the printable Mermaid document instead.
  const url="./plots/g1-system-2026.svg";
  fetch(url,{method:"HEAD"}).then(r=>{
    if(r.ok)window.open(url,"_blank","noopener,noreferrer");
    else window.open("https://github.com/RV-crtl/Digital-Hearing-Aid-G1/blob/main/Documentation/PRINTABLE_SYSTEM_DIAGRAM.md","_blank","noopener,noreferrer");
  }).catch(()=>window.print());
});
