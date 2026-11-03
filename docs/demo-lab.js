/* G1 actual assessment test runner — source-backed, local-only, NO invented PASSes.
 * The test descriptions are versioned copies of Assessment/test_cases.json.
 * docs/test_demo_contract.py ensures they remain byte-for-byte equivalent data.
 */
(()=>{
  "use strict";
  const root=document.getElementById("g1DemoLab");
  if(!root)return;
  const $=(id)=>document.getElementById(id);
  const list=$("g1DemoCases"),message=$("g1DemoMessage"),progress=$("g1DemoProgress");
  const FIELDS=["g1FirmwareSha","g1PcbRev","g1Operator"];
  const STATES=["NOT RUN","PASS","FAIL","BLOCKED"];
  const KEY="g1-local-demo-v2";
  let cases=[],record={};
  try { const raw=localStorage.getItem(KEY);if(raw)record=JSON.parse(raw); }catch(_){record={};}
  for(const id of FIELDS){if(typeof record[id]==="string")$(id).value=record[id];}
  const node=(tag,cls,content)=>{const el=document.createElement(tag);if(cls)el.className=cls;if(content!==undefined)el.textContent=String(content);return el;};
  const tell=(value)=>{message.textContent=value;};
  const stateFor=id=>STATES.includes(record[id]?.status)?record[id].status:"NOT RUN";
  const noteFor=id=>typeof record[id]?.note==="string"?record[id].note:"";
  const store=()=>{for(const id of FIELDS)record[id]=$(id).value;try{localStorage.setItem(KEY,JSON.stringify(record));}catch(_){tell("Browser storage unavailable. Export now to retain your observations.");}};
  const count=()=>{const n=cases.filter(t=>stateFor(t.id)!=="NOT RUN"&&noteFor(t.id).trim()).length;progress.textContent=n+" of "+cases.length+" documented";};
  for(const id of FIELDS)$(id).addEventListener("change",store);
  const addLine=(parent,text)=>parent.appendChild(node("li","",text));
  function render(){
    list.replaceChildren();
    for(const item of cases){
      const det=node("details","g1-case");det.id="demo-"+item.id;
      const sum=node("summary");sum.append(node("span","g1-test-id",item.id),node("strong","",item.name),node("small","",item.minutes+" min"));
      det.appendChild(sum);
      const body=node("div","g1-case-body");
      body.append(node("p","", "Requirements: "+item.requirements.join(", ")));
      body.append(node("p","", "Prerequisites: "+item.prerequisites));
      for(const [label,steps] of [["Procedure",item.procedure],["Expected external behaviour",item.expected]]){
        body.appendChild(node("h4","",label));
        const ol=node("ol");for(const text of steps)addLine(ol,text);body.appendChild(ol);
      }
      body.appendChild(node("p","", "Acceptance gate: "+item.gate));
      const controls=node("div","g1-case-controls"),selLabel=node("label","", "Observed result");
      const select=node("select");select.setAttribute("aria-label",item.id+" observed result");
      for(const state of STATES){const o=node("option","",state);o.value=state;select.appendChild(o);}
      select.value=stateFor(item.id);
      select.addEventListener("change",()=>{
        record[item.id]={status:select.value,note:note.value};
        store();count();
        if(select.value==="PASS"&&!note.value.trim())tell("A PASS needs a real external observation; it will not be exported without one.");
        else tell("Observation saved locally. This is not an assessed PASS certificate.");
      });
      selLabel.appendChild(select);controls.appendChild(selLabel);body.appendChild(controls);
      const label=node("label","g1-case-note","What actually happened?");
      const note=node("textarea");note.placeholder="Measured/observed external behaviour, fixture, power source and any fault (do not invent results)";note.maxLength=2000;
      note.value=noteFor(item.id);
      note.addEventListener("input",()=>{record[item.id]={status:select.value,note:note.value};store();count();});
      label.appendChild(note);body.appendChild(label);
      det.appendChild(body);list.appendChild(det);
    }
    count();
  }
  $("g1DemoReset").addEventListener("click",()=>{
    if(!window.confirm("Clear only the local test notes in this browser?"))return;
    record={};for(const id of FIELDS)$(id).value="";
    store();render();tell("Local notes cleared. Source test procedures remain unchanged.");
  });
  $("g1DemoExport").addEventListener("click",()=>{
    if(!cases.length)return;
    for(const item of cases){const row=record[item.id]||{};
      if(row.status==="PASS"&&!String(row.note||"").trim()){
        tell(item.id+" is marked PASS without an actual observation. Add evidence before export.");
        document.getElementById("demo-"+item.id)?.scrollIntoView({block:"center",behavior:"smooth"});
        return;
      }
    }
    const results=cases.map(t=>({id:t.id,requirements:t.requirements,reported_status:stateFor(t.id),notes:noteFor(t.id)}));
    const data={label:"DRAFT local G1 black-box observations — NOT independently verified",exported_at:new Date().toISOString(),
      firmware_sha:$("g1FirmwareSha").value,pcb_revision:$("g1PcbRev").value,operator:$("g1Operator").value,results};
    const url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:"application/json"}));
    const anchor=node("a");anchor.href=url;anchor.download="G1_black_box_observations_"+new Date().toISOString().slice(0,10)+".json";
    document.body.appendChild(anchor);anchor.click();anchor.remove();
    setTimeout(()=>URL.revokeObjectURL(url),1000);
    tell("Draft observations exported. Physical evidence and supervisor sign-off are separate.");
  });
  fetch("./data/g1-demo-tests.json",{cache:"no-cache"}).then(r=>{if(!r.ok)throw Error(r.status);return r.json();}).then(data=>{
    if(!Array.isArray(data)||data.length!==7||data.some((t,i)=>t.id!=="T"+(i+1)||!Array.isArray(t.procedure)))throw Error("test-plan source invalid");
    cases=data;render();tell("Current 20-minute source-backed plan loaded. Results stay in your browser.");
  }).catch(()=>{
    list.textContent="The interactive test data is unavailable. Use the canonical GitHub black-box test plan linked above.";
    tell("Could not load source JSON. No substitute test results were created.");
  });
})();
