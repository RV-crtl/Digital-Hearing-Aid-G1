/* G1 System Atlas — interactive design and evidence explorer.
 * Signal diagrams are architectural, NOT live hardware telemetry.
 * Preference simulation models the portable C learner; does not control a device.
 */
"use strict";
const ROOT="https://github.com/RV-crtl/Digital-Hearing-Aid-G1/blob/main/";
const nodes={
 pack:{type:"power",label:"2S LiFePO₄",sub:"2 × 26650 · 3.25 V nominal each",detail:"Your intended standalone power source: two 26650 LiFePO₄ cells in series, approximately 6.50 V nominal as a pair. Power conversion to the board's operating rails, cell matching, under-voltage protection and any charge/balance strategy require verification on the actual assembly.",path:"Hardware/Original_KiCad/Power.kicad_sch"},
 rails:{type:"power",label:"Protection + rails",sub:"Power path · regulated 3.3 V",detail:"Battery input, protection and regulator operation must be checked from the original Power sheet. A USB connector or regulator does not establish a safe 2-cell charger. Check the minimum and maximum cell/pack voltages against every device rating.",path:"Hardware/Original_KiCad/Power.kicad_sch"},
 bt:{type:"input",label:"Bluetooth module",sub:"Radio → SAI1A PCM",detail:"Bluetooth audio reaches the STM32 through serial audio SAI1A; source health may be supervised separately over a control interface. Audio samples and link-control messages are different flows.",path:"Core/Src/connectivity_service.c"},
 tc:{type:"input",label:"Telecoil + analogue front end",sub:"Induction → conditioning → SAI2A",detail:"Telecoil coil voltages pass through analogue conditioning and a digital audio input. Check the exact circuit, clocking and SAI data framing before substituting modules.",path:"Hardware/Original_KiCad/Telecoil_Buffer_Circuits.kicad_sch"},
 mic:{type:"candidate",label:"Digital microphone",sub:"PDM → DFSDM1 · PCB design",detail:"The provided KiCad pin map includes DFSDM1_CKOUT and DFSDM1_DATIN2 for the intended digital microphone path. The checked-in operating firmware does not establish a validated microphone-capture / routing path. This is a PCB integration gate, not a finished input in CI.",path:"Hardware/Original_KiCad/Audio.kicad_sch"},
 rx:{type:"dma",label:"DMA RX buffers",sub:"Half/full interrupts · 24-bit SAI words",detail:"STM32 SAI RX DMA fills circular word buffers. Half/full callbacks snapshot completed halves into bounded capture rings BEFORE waking AudioTask. The transport interprets sign, byte width and stereo-to-mono conversion. ISR must be short and non-blocking.",path:"Core/Src/demo_app.c"},
 notify:{type:"rtos",label:"FreeRTOS notify",sub:"ISR → AudioTask · no busy-wait",detail:"DMA callback publishes a complete capture block, performs a publication barrier and gives the AudioTask a notification. AudioTask drains a bounded number of blocks and uses ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5U)) while idle.",path:"Core/Src/demo_app.c"},
 audio:{type:"rtos",label:"AudioTask",sub:"Priority 7 · single audio owner",detail:"Highest normal audio-task priority. Owns source FSM and PCM movement, optional DSP / limiter policies, guardian and output queue. It must not block on UART, website activity or slow storage.",path:"Core/Src/demo_app.c"},
 dsp:{type:"dsp",label:"Signal processing",sub:"48 kHz nominal · profiles + saturation",detail:"Sample routing, 24-to-16-bit transport, gain and audio dynamics. Real task timing and audio quality must be measured at the assembly; source-only CI confirms portable arithmetic but not measured SPL.",path:"Core/Src/audio_dynamics.c"},
 pref:{type:"candidate",label:"Context preference learner",sub:"27 contextual buckets · RAM only",detail:"The tested portable C learner indexes source × loudness × approximate frequency band, records bounded explicit user volume corrections, applies feedback throttling and confidence gating, and recommends attenuation at or below unity. It is NOT connected to the custom-board volume control yet.",path:"Core/Src/preference_learner.c"},
 out:{type:"dma",label:"SAI2B TX DMA",sub:"PCM FIFO → stereo frames → amplifier",detail:"AudioTask supplies processed mono audio to the elastic FIFO; output is duplicated into SAI2B stereo DMA frames and sent to the audio amplifier / transducer. Underrun counters and startup fades are relevant failure metrics.",path:"Core/Src/speaker_output.c"},
 controls:{type:"candidate",label:"PCB controls",sub:"Rotary + touch/I²C · map required",detail:"The PCB pad map includes encoder PA0/PA1 and an I²C-connected user-interface footprint. Current G5 standalone gestures use Nucleo PC13/PA5, absent on the custom PCB; physical controls and ownership must be ported before claiming this route operational.",path:"Hardware/STM32_U13_PINMAP.csv"},
 ctl:{type:"rtos",label:"ControlTask",sub:"Priority 6 · queue producer",detail:"Samples debounced input, rejects invalid requests and sends bounded GroupEvent structures via xQueueSend(...,0). Never modifies AudioTask-owned DSP or output buffers directly.",path:"Core/Src/group_ui.c"},
 bus:{type:"rtos",label:"GroupEvent queue",sub:"8 entries · copied values",detail:"The group control queue transports small {type,value} events from ControlTask to AudioTask. The queue is bounded, sends do not block, and drop counters surface overload. Audio data instead moves in the DMA/capture rings.",path:"Core/Src/demo_app.c"},
 supervise:{type:"rtos",label:"Bluetooth supervisor",sub:"Priority 5 · state + faults",detail:"Bluetooth task handles link supervision; it does not directly own PCM DMA. Loss of source should become a state machine event and leave output predictable.",path:"Core/Src/connectivity_controller.c"},
 background:{type:"rtos",label:"Background task",sub:"Lower priority · no audio ownership",detail:"Background work and telemetry must be bounded and must not keep AudioTask from meeting its deadlines. Synthetic stress workloads are excluded from the operating build.",path:"Core/Src/demo_app.c"}
};
const routes={
 bluetooth:["bt","rx","notify","audio","dsp","out"],
 telecoil:["tc","rx","notify","audio","dsp","out"],
 microphone:["mic","rx","notify","audio","dsp","out"]
};
const atlas=document.getElementById("atlasMount");
if(atlas){
 atlas.innerHTML=`<div class="atlas-intro"><div><span class="micro-label">INTERACTIVE SYSTEM DIGITAL TWIN · ARCHITECTURE, NOT A LIVE DEVICE</span><h2>Every signal. <em>Every owner.</em></h2><p>One map for the final custom PCB: choose an input and follow the sample from physical interface through DMA, scheduler, DSP and output. Inspect the control plane, power chain and adaptive learning separately.</p></div><span class="atlas-stamp">SOURCE-TRACED / 2026</span></div>
 <div class="atlas-root">
  <div class="atlas-topbar"><div class="atlas-source" role="group" aria-label="Input to trace"><button data-path="bluetooth" class="active">◉ Bluetooth</button><button data-path="telecoil">◎ Telecoil</button><button data-path="microphone">◌ Microphone design</button></div><span>◉ source-integrated &nbsp; ◌ design-only</span></div>
  <div class="atlas-caption">AUDIO DATA PLANE <span>SAI / DMA / FreeRTOS · TARGET 48 kHz</span></div>
  <div class="atlas-path" id="atlasPath" role="group" aria-label="Interactive signal path"></div>
  <div class="atlas-support"><div class="atlas-support-col"><div class="atlas-caption">CONTROL PLANE <span>SMALL MESSAGES, NOT AUDIO SAMPLES</span></div><div class="atlas-minigrid" id="atlasControl"></div></div><div class="atlas-support-col"><div class="atlas-caption">POWER / REGULATION <span>STANDALONE CUSTOM PCB</span></div><div class="atlas-minigrid" id="atlasPower"></div></div></div>
  <div class="atlas-inspector" aria-live="polite"><div><span class="micro-label" id="atlasType">SELECT A BLOCK</span><h3 id="atlasTitle">Select any block to inspect.</h3><p id="atlasExplain">Each node is linked to the original G1 source or KiCad file.</p></div><a id="atlasCode" href="${ROOT}Core/Src/demo_app.c" target="_blank" rel="noopener noreferrer">Inspect actual source ↗</a></div>
 </div>
 <div class="atlas-section-heading"><div><span class="micro-label">REAL-TIME KERNEL</span><h3>One scheduler. Clear ownership.</h3></div><p>Click a task to inspect the queue, buffer and peripheral it owns.</p></div>
 <div class="atlas-taskgrid" id="atlasTaskgrid"></div>
 <div class="atlas-footnote">Timing figures are architectural targets. The submitted rubric requires real measured deadlines, stack headroom, fault recovery, and user-visible standalone behaviour. <a href="${ROOT}Documentation/G9_REPOSITORY_INTEGRITY_AUDIT.md" target="_blank" rel="noopener noreferrer">Software verification ↗</a></div>
 <div class="atlas-section-heading atlas-learning-head"><div><span class="micro-label">ONLINE ADAPTIVE PREFERENCE MODEL</span><h3>Let feedback shape a context.</h3></div><p>Interactive deterministic example of the portable C learner. A training simulator, <strong>not connected to the hearing aid</strong>.</p></div>
 <div class="atlas-learning" id="atlasLearning">
  <div class="atlas-learn-controls">
   <label>Scene loudness <strong id="sceneLevelText">Moderate</strong><input id="atlasLoudness" type="range" min="0" max="2" value="1"></label>
   <label>Spectral content <strong id="spectralText">Mid-range</strong><input id="atlasSpectrum" type="range" min="0" max="2" value="1"></label>
   <label>Input context <select id="atlasContext"><option value="0">Bluetooth</option><option value="1">Telecoil</option><option value="2">Microphone (design)</option></select></label>
   <div class="atlas-feedback"><button type="button" id="prefDown" class="atlas-feedback-down">− User lowers volume</button><button type="button" id="prefUp" class="atlas-feedback-up">+ User raises volume</button></div><button type="button" id="prefReset" class="atlas-reset">Reset demo learning</button>
  </div>
  <div class="atlas-learn-output"><span class="micro-label">BOUNDED ADAPTATION / EXAMPLE</span><div class="atlas-big-number" id="atlasGain">100<small>% relative level</small></div><div class="atlas-meter"><span id="atlasFill"></span></div><div class="atlas-learning-metrics"><span><small>Observations</small><strong id="atlasSamples">0</strong></span><span><small>Context confidence</small><strong id="atlasConfidence">Untrained</strong></span><span><small>Policy</small><strong>Attenuation-only</strong></span></div><p id="atlasLearnMessage">The model starts neutral. Give feedback multiple times in the same context to learn a bounded preference; other contexts remain independent.</p><div class="atlas-bands"><span>BASS</span><i></i><span>MID</span><i></i><span>TREBLE</span></div></div>
 </div>
 <div class="atlas-warning"><strong>Production boundary.</strong> The source contains the real on-device-sized preference model and repeatable unit tests. A custom-PCB feedback event path, nonvolatile opt-in, electrical protection, acoustic gain calibration and IEC/regulatory evidence are still separate requirements. A positive correction never causes gain above unity in this model.</div>`;
 const $=(s,el=atlas)=>el.querySelector(s);
 const $$=(s,el=atlas)=>[...el.querySelectorAll(s)];
 const safeType=t=>({power:"POWER RAIL",input:"SIGNAL INPUT",candidate:"DESIGN / INTEGRATION GATE",dma:"DMA & MEMORY",rtos:"FreeRTOS TASK/CONTROL",dsp:"SIGNAL PROCESSING"}[t]||t);
 function inspect(id){
  const n=nodes[id];if(!n)return;
  $("#atlasType").textContent=safeType(n.type);
  $("#atlasTitle").textContent=n.label;
  $("#atlasExplain").textContent=n.detail;
  $("#atlasCode").href=ROOT+n.path.split("/").map(encodeURIComponent).join("/");
  $$(".atlas-node").forEach(x=>x.classList.toggle("selected",x.dataset.node===id));
 }
 function node(id,small=false){
  const n=nodes[id],button=document.createElement("button");
  button.type="button";button.dataset.node=id;button.className="atlas-node atlas-node--"+n.type+(small?" atlas-node--small":"");
  button.innerHTML='<span class="atlas-node-type">'+safeType(n.type)+'</span><strong>'+n.label+'</strong><small>'+n.sub+'</small><span class="atlas-node-open">↗</span>';
  button.addEventListener("click",()=>inspect(id));return button;
 }
 function mount(selector,ids){const el=$(selector);el.replaceChildren();ids.forEach((id,i)=>{el.append(node(id,selector!=="#atlasPath"));if(selector==="#atlasPath"&&i<ids.length-1){const arrow=document.createElement("span");arrow.className="atlas-flow-arrow";arrow.textContent="→";arrow.setAttribute("aria-hidden","true");el.append(arrow)}})}
 mount("#atlasControl",["controls","ctl","bus","pref"]);
 mount("#atlasPower",["pack","rails"]);
 const tasks=[["audio","07","AudioTask"],["ctl","06","ControlTask"],["supervise","05","BtTask"],["background","02","Background"]];
 const grid=$("#atlasTaskgrid");
 tasks.forEach(([id,priority])=>{
  const cell=document.createElement("button");cell.type="button";cell.className="atlas-task";
  const n=nodes[id];cell.innerHTML='<span class="atlas-prio">P'+priority+'</span><strong>'+n.label+'</strong><small>'+n.sub+'</small><span class="atlas-task-marker">VIEW OWNER ↗</span>';
  cell.addEventListener("click",()=>{inspect(id);$("#atlasTitle").scrollIntoView({behavior:"smooth",block:"center"})});grid.append(cell)
 });
 function choose(source){
  mount("#atlasPath",routes[source]);
  $$(".atlas-source button").forEach(x=>x.classList.toggle("active",x.dataset.path===source));
  inspect(routes[source][0]);
 }
 $$(".atlas-source button").forEach(btn=>btn.addEventListener("click",()=>choose(btn.dataset.path)));
 choose("bluetooth");
 // Mirrors C's bounded directional contextual learner; event cooldown here is
 // deliberately omitted to allow manual educational feedback at user pace.
 const model=Array.from({length:27},()=>({bias:0,count:0}));
 const loudLabels=["Quiet","Moderate","Loud"],bandLabels=["Bass / low","Mid-range","Treble / high"];
 const active=()=>Number($("#atlasContext").value)*9+Number($("#atlasLoudness").value)*3+Number($("#atlasSpectrum").value);
 function paint(){
  const cell=model[active()];
  $("#sceneLevelText").textContent=loudLabels[Number($("#atlasLoudness").value)];
  $("#spectralText").textContent=bandLabels[Number($("#atlasSpectrum").value)];
  const q15=cell.count>=2&&cell.bias<0?Math.max(24576,32768+cell.bias*1024):32768;
  const percent=Math.round(q15/32768*100);
  $("#atlasGain").innerHTML=String(percent)+'<small>% relative level</small>';
  $("#atlasFill").style.width=percent+"%";
  $("#atlasSamples").textContent=String(cell.count);
  $("#atlasConfidence").textContent=cell.count<2?"Untrained":cell.count<5?"Learning":"Context established";
  $("#atlasLearnMessage").textContent=cell.count<2?"Two or more deliberate corrections are required for this context.":cell.bias<0?"Learned from repeated user volume reductions: limited attenuation only.":cell.bias>0?"User raises the level: recommendation returns toward unity, never above it.":"Equal feedback: neutral adjustment.";
 }
 function feedback(delta){const cell=model[active()];cell.bias=Math.max(-8,Math.min(8,cell.bias+delta));cell.count=Math.min(32,cell.count+1);paint();}
 $("#prefDown").addEventListener("click",()=>feedback(-1));
 $("#prefUp").addEventListener("click",()=>feedback(1));
 $("#prefReset").addEventListener("click",()=>{for(const p of model){p.bias=0;p.count=0;}paint()});
 ["atlasLoudness","atlasSpectrum","atlasContext"].forEach(id=>$("#"+id).addEventListener("input",paint));
 paint();
}
