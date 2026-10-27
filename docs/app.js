/* G1 LAB — presentation layer only. Source of truth remains the repository. */
"use strict";
const REPO = "https://github.com/RV-crtl/Digital-Hearing-Aid-G1";
const RAW = "https://raw.githubusercontent.com/RV-crtl/Digital-Hearing-Aid-G1/main/";
const $ = (q, root = document) => root.querySelector(q);
const $$ = (q, root = document) => [...root.querySelectorAll(q)];
const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
const sheets = [
  {name:"Connectivity",file:"Connectivity.kicad_sch",group:"control",eyebrow:"WIRELESS / INPUT",desc:"Interface and Bluetooth connectivity sheet"},
  {name:"Audio",file:"Audio.kicad_sch",group:"audio",eyebrow:"SIGNAL / ROUTING",desc:"Signal chain and audio subsystem"},
  {name:"Telecoil Buffer Circuits",file:"Telecoil_Buffer_Circuits.kicad_sch",group:"audio",eyebrow:"INDUCTION / FRONT END",desc:"Telecoil signal conditioning"},
  {name:"Analog Front End",file:"Analog_Frontend.kicad_sch",group:"audio",eyebrow:"ANALOG / FRONT END",desc:"Input analogue circuitry"},
  {name:"Amplifier",file:"Amplifier.kicad_sch",group:"audio",eyebrow:"AUDIO / POWER STAGE",desc:"Amplifier design"},
  {name:"Output",file:"Output.kicad_sch",group:"audio",eyebrow:"OUTPUT / TRANSDUCER",desc:"Output path circuitry"},
  {name:"Power",file:"Power.kicad_sch",group:"power",eyebrow:"POWER / DISTRIBUTION",desc:"Supply and power design"},
  {name:"Processor",file:"Processor.kicad_sch",group:"control",eyebrow:"MCU / CORE",desc:"Processor circuitry"},
  {name:"STM32",file:"STM32.kicad_sch",group:"control",eyebrow:"MCU / SIGNALS",desc:"STM32 hardware sheet"},
  {name:"User Interface",file:"User Interface.kicad_sch",group:"control",eyebrow:"USER / CONTROLS",desc:"Physical interface circuitry"},
  {name:"Active Bandpass Filter",file:"Active_Bandpass_Filter.kicad_sch",group:"audio",eyebrow:"FILTER / ANALOG",desc:"Band-pass filter circuitry"},
  {name:"Mid-Supply Buffer",file:"Mid-Supply_Buffer.kicad_sch",group:"power",eyebrow:"BIAS / ANALOG",desc:"Mid-supply buffer"},
  {name:"CCP",file:"CCP.kicad_sch",group:"control",eyebrow:"CONTROL / BOARD",desc:"CCP design sheet"},
  {name:"Main design",file:"260510-Digital_hearing_aid.kicad_sch",group:"control",eyebrow:"ROOT / KICAD",desc:"Hierarchical schematic entry point"}
];
const gallery = $("#schematicsGrid");
const dialog = $("#schematicDialog");
const dialogBody = $("#dialogBody");
const escapeHTML = s => s.replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
function circuitIllustration(seed){
  const tracks = [];
  for(let i=0;i<11;i++){
    const x=35+((seed*23+i*61)%315), y=26+((seed*13+i*47)%150);
    const dx= (i%2===0 ? 54 : -39), dy=i%3===0 ? 36 : -24;
    tracks.push('<path d="M'+x+' '+y+' h'+dx+' v'+dy+' h'+(i%2?17:-18)+'" />');
    tracks.push('<circle cx="'+x+'" cy="'+y+'" r="3" />');
  }
  const positions=[[95,64],[252,115],[180,164],[325,52]];
  return '<svg viewBox="0 0 430 220" aria-hidden="true" xmlns="http://www.w3.org/2000/svg"><defs><linearGradient id="g'+seed+'" x1="0" x2="1"><stop stop-color="#66eecb"/><stop offset="1" stop-color="#7b9aff"/></linearGradient></defs><g fill="none" stroke="url(#g'+seed+')" stroke-width="1.3" stroke-linecap="round" opacity=".68">'+tracks.join("")+'</g><g stroke="#799eb0" stroke-width="1.3" fill="#142e3b">'+positions.map(([x,y],n)=>'<rect x="'+(x-22)+'" y="'+(y-13)+'" width="44" height="26" rx="3"/><path d="M'+(x-28)+' '+(y-6)+' h6 M'+(x+22)+' '+(y+6)+' h6"/>').join("")+'</g><g fill="#8ff3d7" font-size="10" font-family="monospace" text-anchor="middle">'+positions.map(([x,y],n)=>'<text x="'+x+'" y="'+(y+3)+'">U'+(n+1)+'</text>').join("")+'</g></svg>';
}
let plots = [];
function plotFor(sheet){
  const norm = s => s.toLowerCase().replace(/[^a-z0-9]+/g,"");
  const target = norm(sheet.file.replace(/\.kicad_sch$/i,""));
  const exact = plots.find(p => norm(p.name||"") === target);
  return exact || plots.find(p=>norm(p.name||"").includes(target) || target.includes(norm(p.name||"")) ) || null;
}
function renderGallery(filter="all"){
  const visible=sheets.filter(s=>filter==="all"||s.group===filter);
  gallery.replaceChildren();
  visible.forEach((item)=>{
    const index=sheets.indexOf(item);
    const plot=plotFor(item);
    const card=document.createElement("button");card.type="button";card.className="schematic-card";card.setAttribute("aria-label","View "+item.name+" schematic");
    const thumb=document.createElement("div");thumb.className="schematic-thumb";
    thumb.innerHTML='<span class="thumb-index">'+String(index+1).padStart(2,"0")+' / '+(plot?"ACTUAL KICAD SVG":"DESIGN MAP")+'</span>'+(plot?'<img alt="KiCad schematic preview" loading="lazy" src="'+encodeURI(plot.path)+'">':circuitIllustration(index+1));
    const detail=document.createElement("div");detail.className="schematic-detail";
    detail.innerHTML="<div><span>"+escapeHTML(item.eyebrow)+"</span><h3>"+escapeHTML(item.name)+"</h3></div><span class='arrow'>↗</span>";
    card.append(thumb,detail);
    card.addEventListener("click",()=>showSheet(item,plot));
    gallery.append(card);
  });
  $("#schematicCount").textContent=visible.length+" design sheets · "+plots.length+" exported SVG views";
}
function showSheet(item,plot){
  $("#dialogTitle").textContent=item.name;
  $("#dialogSource").href=REPO+"/blob/main/Hardware/Original_KiCad/"+encodeURIComponent(item.file).replace(/%2F/g,"/");
  $("#dialogInfo").textContent=plot?"Native KiCad SVG · click image to magnify":"Source design · full SVG unavailable in current build";
  dialogBody.replaceChildren();
  if(plot){
    const img=document.createElement("img");
    img.alt="Full resolution KiCad SVG plot for "+item.name;
    img.src=encodeURI(plot.path);
    img.loading="eager";
    img.addEventListener("click",()=>img.classList.toggle("zoomed"));
    img.addEventListener("error",()=>{dialogBody.textContent="Schematic plot could not be loaded. Open the KiCad source file instead.";});
    dialogBody.append(img);
  }else{
    const info=document.createElement("div");info.className="placeholder";
    info.innerHTML=circuitIllustration(sheets.indexOf(item)+1)+'<h3>'+escapeHTML(item.name)+'</h3><p>This abstract circuit illustration is only a navigation preview, not an electrical drawing. Click “Open KiCad source” for the actual design. A Pages build with KiCad generates viewable original SVG plots.</p>';
    dialogBody.append(info);
  }
  if(typeof dialog.showModal==="function")dialog.showModal();else dialog.setAttribute("open","");
}
$("#dialogClose").addEventListener("click",()=>dialog.close());
dialog.addEventListener("click",e=>{if(e.target===dialog)dialog.close();});
$$(".filter-btn").forEach(btn=>btn.addEventListener("click",()=>{
  $$(".filter-btn").forEach(x=>x.classList.toggle("active",x===btn));renderGallery(btn.dataset.filter);
}));
renderGallery();
fetch("./plots/manifest.json",{cache:"no-store"}).then(r=>r.ok?r.json():null).then(data=>{
  if(data && Array.isArray(data.schematics)){
    plots=data.schematics.filter(x=>typeof x.name==="string"&&typeof x.path==="string"&&x.path.startsWith("plots/schematics/")&&!x.path.includes(".."));
    renderGallery($(".filter-btn.active")?.dataset.filter||"all");
  }
}).catch(()=>{});

$$(".source-btn").forEach(button=>button.addEventListener("click",()=>{
  $$(".source-btn").forEach(x=>x.classList.toggle("active",x===button));
  const coil=button.dataset.source==="telecoil";
  $("#chainInputTitle").textContent=coil?"Telecoil induction":"Bluetooth audio";
  $("#chainInputText").textContent=coil?"Inductive hearing-loop signals and telecoil detection.":"External Bluetooth audio input and link observations.";
  $("#selectedInput").textContent=coil?"Telecoil":"Bluetooth";
  $(".chain-node[data-node=input] .node-icon").textContent=coil?"◎":"◉";
}));

const codePreview=$("#codePreview"), codeStatus=$("#codeStatus"), codeLink=$("#codeOnGithub");
let inflight, currentCode="";
function decorate(line){
  const re=/(\/\/.*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\b(?:static|const|void|bool|return|if|else|for|while|switch|case|break|continue|typedef|struct|enum|volatile|sizeof|unsigned|signed|true|false|NULL)\b|\b(?:uint8_t|uint16_t|uint32_t|int16_t|int32_t|size_t|int64_t|float|double|uint64_t)\b|\b(?:\d+U?L?|0x[0-9a-fA-F]+)\b)/g;
  let html="",at=0,match;
  while((match=re.exec(line))!==null){
    html+=escapeHTML(line.slice(at,match.index));
    const token=match[0];
    const cls=token.startsWith("//")?"syntax-comment":token.startsWith('"')||token.startsWith("'")?"syntax-string":/^(?:uint|int|size_t|float|double)/.test(token)?"syntax-type":/^(?:\d|0x)/.test(token)?"syntax-number":"syntax-key";
    html+='<span class="'+cls+'">'+escapeHTML(token)+'</span>';
    at=match.index+token.length;
  }
  return html+escapeHTML(line.slice(at));
}
function putCode(text){
  const lines=text.split(/\r?\n/).slice(0,175);
  codePreview.innerHTML=lines.map((line,i)=>'<span class="code-line"><span class="code-no">'+String(i+1).padStart(3,"0")+'</span>'+decorate(line)+'</span>').join("");
}
async function loadCode(file){
  if(inflight)inflight.abort();
  inflight=new AbortController();
  $("#activeFile").textContent=file;
  codeLink.href=REPO+"/blob/main/"+file;
  codePreview.textContent="Retrieving original C source from GitHub…";
  codeStatus.textContent="Loading real source…";
  try{
    const response=await fetch(RAW+file,{signal:inflight.signal});
    if(!response.ok)throw Error("HTTP "+response.status);
    const source=await response.text();
    if(source.length>750000)throw Error("File exceeds preview size limit");
    currentCode=source;putCode(source);
    codeStatus.textContent="Original file · showing first "+Math.min(175,source.split(/\r?\n/).length)+" lines";
  }catch(err){
    if(err.name==="AbortError")return;
    codePreview.textContent="Live source preview unavailable. Open this file on GitHub to inspect the current implementation.";
    codeStatus.textContent="GitHub source unavailable · link remains available";
  }
}
$$(".file-tab").forEach(btn=>btn.addEventListener("click",()=>{
  $$(".file-tab").forEach(x=>x.classList.toggle("active",x===btn));
  loadCode(btn.dataset.file);
}));
loadCode("Core/Src/demo_app.c");
const nav=$("#mobileNav");nav.addEventListener("click",()=>{
  const expanded=nav.getAttribute("aria-expanded")==="true";
  nav.setAttribute("aria-expanded",String(!expanded));
  $(".nav-links").classList.toggle("open",!expanded);
});
$$(".nav-links a").forEach(a=>a.addEventListener("click",()=>{
  $(".nav-links").classList.remove("open");nav.setAttribute("aria-expanded","false");
}));

/* The model viewer loads separately so that CDN/WebGL failures never disable the rest of the site. */
import("./viewer.js").then(({bootViewer})=>bootViewer({reducedMotion})).catch(err=>{
  $("#modelStatus").textContent="3D preview requires WebGL and Three.js; source CAD is linked below.";
  $("#modelMode").textContent="Viewer unavailable · source assets remain accessible";
  $("#modelViewport").setAttribute("aria-label","3D viewer not available");
  console.warn("G1 3D viewer:",err);
});
