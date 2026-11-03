/* G1 WebGL assembly explorer. Loads the authentic supplied 3MF CAD assembly.
   Any conceptual geometry is not used as the hosted model. */
import * as THREE from "three";
import {OrbitControls} from "three/addons/controls/OrbitControls.js";
import {GLTFLoader} from "three/addons/loaders/GLTFLoader.js";
import {ThreeMFLoader} from "three/addons/loaders/3MFLoader.js";

const $=id=>document.getElementById(id);
export function bootViewer({reducedMotion=false}={}){
  const wrap=$("modelViewport");
  const renderer=new THREE.WebGLRenderer({antialias:true,alpha:true,powerPreference:"high-performance"});
  renderer.setPixelRatio(Math.min(window.devicePixelRatio||1,2));
  renderer.setClearColor(0x071422,0);
  renderer.toneMapping=THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure=1.45;
  wrap.appendChild(renderer.domElement);

  const scene=new THREE.Scene();
  const camera=new THREE.PerspectiveCamera(39,1,.1,1600);
  const viewPosition=new THREE.Vector3(142,-168,142);
  camera.position.copy(viewPosition);
  scene.add(new THREE.HemisphereLight(0xf1fbff,0x2c4f58,2.3));
  const key=new THREE.DirectionalLight(0xc8ffef,3.4);key.position.set(-105,-45,155);scene.add(key);
  const fill=new THREE.DirectionalLight(0x7798ff,2.2);fill.position.set(80,75,66);scene.add(fill);
  const rim=new THREE.DirectionalLight(0xffd6a0,1.4);rim.position.set(-50,100,-75);scene.add(rim);
  const grid=new THREE.GridHelper(210,21,0x25525a,0x1e3449);grid.rotation.x=Math.PI/2;grid.position.z=-32;
  grid.material.transparent=true;grid.material.opacity=.17;scene.add(grid);
  const controls=new OrbitControls(camera,renderer.domElement);
  controls.enableDamping=true;controls.dampingFactor=.07;controls.minDistance=50;controls.maxDistance=440;
  controls.maxPolarAngle=Math.PI;controls.target.set(0,0,0);
  let assembly=null,loadedCAD=false,manualInput=false;
  let meshes=[],selected=null,pointerStart=null;
  let triangleCount=0;
  let explodeNodes=[];
  const explodeVectors=new WeakMap();
  const selectedMats=[];
  const raycaster=new THREE.Raycaster(),pointer=new THREE.Vector2();
  const search=$("cadSearch"),parts=$("cadParts"),stats=$("cadStats");
  const startPos=new WeakMap();
  let currentView="iso";
  const presets={iso:[142,-168,142],top:[0,-4,230],side:[213,-18,56]};
  let transitioning=false;
  let fitDistance=220;
  const targetCamera=new THREE.Vector3(...presets.iso);
  const status=$("modelStatus"),mode=$("modelMode");
  function clearSelection(){
    for(const {material,emissive} of selectedMats){if(material.emissive)material.emissive.setHex(emissive);}
    selectedMats.length=0;selected=null;
  }
  function selectPart(obj){
    clearSelection();selected=obj||null;
    if(!selected)return;
    for(const mat of (Array.isArray(selected.material)?selected.material:[selected.material])){
      if(mat?.emissive){selectedMats.push({material:mat,emissive:mat.emissive.getHex()});mat.emissive.setHex(0x17684e);}
    }
    $("modelStatus").textContent="Selected "+(selected.name||"unnamed mesh")+" · click elsewhere or Show full assembly";
    renderParts();
  }
  function renderParts(){
    if(!parts||!stats)return;
    const query=(search?.value||"").trim().toLowerCase();
    const matches=query?meshes.filter(obj=>(obj.name||"unnamed").toLowerCase().includes(query)):meshes;
    stats.textContent=meshes.length.toLocaleString()+" render meshes · "+triangleCount.toLocaleString()+" triangles · "+matches.length+" matches";
    parts.replaceChildren();
    for(const obj of matches.slice(0,48)){
      const button=document.createElement("button");button.type="button";
      button.textContent=obj.name||"unnamed mesh";
      button.setAttribute("aria-pressed",String(obj===selected));
      button.addEventListener("click",()=>selectPart(obj));
      parts.appendChild(button);
    }
    if(matches.length>48){const more=document.createElement("span");more.textContent="+"+(matches.length-48)+" more · refine search";parts.append(more);}
  }
  function fitCamera(){
    if(!assembly)return;
    const b=new THREE.Box3().setFromObject(assembly),size=b.getSize(new THREE.Vector3());
    const maxDimension=Math.max(size.x,size.y,size.z,60);
    const fovRadians=camera.fov*Math.PI/180;
    fitDistance=Math.max(160, maxDimension/(2*Math.tan(fovRadians/2))*1.4);
    controls.maxDistance=Math.max(1800,fitDistance*5);
    controls.minDistance=Math.max(3,maxDimension*.006);
    camera.far=Math.max(8000,fitDistance*8);camera.updateProjectionMatrix();
    presets.iso=[fitDistance*.75,-fitDistance*.80,fitDistance*.70];
    presets.top=[0,-fitDistance*.01,fitDistance*1.2];
    presets.side=[fitDistance*1.2,-fitDistance*.05,fitDistance*.20];
  }
  function disposeAssembly(old){
    if(!old)return;
    const geo=new Set(),mat=new Set();
    old.traverse(obj=>{if(obj.isMesh){if(obj.geometry)geo.add(obj.geometry);for(const m of (Array.isArray(obj.material)?obj.material:[obj.material]))if(m)mat.add(m);}});
    for(const g of geo)g.dispose();for(const m of mat)m.dispose();
  }
  function setAssembly(next,isCAD){
    clearSelection();
    if(assembly){scene.remove(assembly);disposeAssembly(assembly);}
    assembly=next;loadedCAD=isCAD;
    const bnd=new THREE.Box3().setFromObject(assembly);
    const center=bnd.getCenter(new THREE.Vector3());
    assembly.position.sub(center);
    assembly.rotation.set(-.08,0,-.12);
    meshes=[];triangleCount=0;
    assembly.traverse(obj=>{
      if(obj.isMesh){
        obj.castShadow=false;obj.receiveShadow=false;
        obj.material=Array.isArray(obj.material)?obj.material.map(mat=>mat.clone()):obj.material.clone();
        meshes.push(obj);
        if(obj.geometry?.index)triangleCount+=Math.floor(obj.geometry.index.count/3);
        else if(obj.geometry?.attributes?.position)triangleCount+=Math.floor(obj.geometry.attributes.position.count/3);
      }
    });
    // Imported STEP-to-3MF objects often have generic names (Imported_1713),
    // so exploding ONLY 'Shell', 'Copper' or 'Components' never moved them.
    // Derive offsets from real geometry instead of guessing semantic labels.
    explodeNodes=assembly.children.length>1?assembly.children:meshes;
    const diag=bnd.getSize(new THREE.Vector3()).length();
    const travel=Math.max(6,Math.min(52,diag*0.065));
    explodeNodes.forEach((node,i)=>{
      startPos.set(node,node.position.clone());
      const box=new THREE.Box3().setFromObject(node);
      let dir=box.getCenter(new THREE.Vector3());
      if(!Number.isFinite(dir.x)||dir.lengthSq()<0.0001){
        const theta=i*2.399963229728653;
        dir=new THREE.Vector3(Math.cos(theta),Math.sin(theta),Math.sin(theta*.6));
      }
      explodeVectors.set(node,dir.normalize().multiplyScalar(travel));
    });
    scene.add(assembly);
    fitCamera();renderParts();updateExplode();
    status.textContent=isCAD?"Authentic supplied CAD assembly · drag to inspect":"CAD model unavailable";
    mode.textContent=isCAD?"Source CAD loaded · object inspector active":"No source geometry loaded";
    resetCamera();
  }
  function updateExplode(){
    if(!assembly)return;
    const amount=Number($("explode").value)/100;
    $("explodeValue").textContent=Math.round(amount*100)+"%";
    // Geometry-based offsets remain deterministic for generic Imported_* CAD bodies.
    // World geometry is never modified; only node transforms are changed.
    for(const node of explodeNodes){
      const base=startPos.get(node),offset=explodeVectors.get(node);
      if(!base||!offset)continue;
      node.position.copy(base).addScaledVector(offset,amount);
    }
  }
  function setWireframe(){
    if(!assembly)return;
    const enabled=$("wireframeToggle").checked;
    assembly.traverse(obj=>{if(obj.isMesh&&obj.material){for(const mat of (Array.isArray(obj.material)?obj.material:[obj.material]))mat.wireframe=enabled;}});
  }
  function showHousing(){
    if(!assembly)return;
    // Do not mislabel unnamed 3MF body groups as a validated housing.
    const housing=assembly.children.filter(n=>/(shell|housing|case)/i.test(n.name||""));
    $("housingToggle").disabled=housing.length===0;
    for(const node of housing)node.visible=$("housingToggle").checked;
    $("housingToggle").title=housing.length?"Toggle source-labelled housing":"Source body names do not identify housing; inspect CAD objects instead";
  }
  function resetCamera(){
    currentView="iso";targetCamera.set(...presets.iso);transitioning=true;
    controls.target.set(0,0,0);
    $$(".view-btn").forEach(b=>b.classList.toggle("active",b.dataset.view==="iso"));
  }
  function setView(view){
    if(!presets[view])return;
    currentView=view;targetCamera.set(...presets[view]);transitioning=true;
    $$(".view-btn").forEach(b=>b.classList.toggle("active",b.dataset.view===view));
  }
  $$(".view-btn").forEach(b=>b.addEventListener("click",()=>setView(b.dataset.view)));
  $("resetModel").addEventListener("click",()=>{fitCamera();resetCamera();});
  search?.addEventListener("input",renderParts);
  $("cadShowAll")?.addEventListener("click",()=>{
    clearSelection();if(search)search.value="";
    for(const obj of meshes)obj.visible=true;renderParts();
    status.textContent="Full CAD assembly visible";resetCamera();
  });
  wrap.addEventListener("pointerdown",e=>{pointerStart={x:e.clientX,y:e.clientY};});
  wrap.addEventListener("pointerup",e=>{
    if(!assembly||!pointerStart||Math.hypot(e.clientX-pointerStart.x,e.clientY-pointerStart.y)>5)return;
    pointerStart=null;
    const rect=renderer.domElement.getBoundingClientRect();
    pointer.set(((e.clientX-rect.left)/rect.width)*2-1,-((e.clientY-rect.top)/rect.height)*2+1);
    raycaster.setFromCamera(pointer,camera);
    const hit=raycaster.intersectObjects(meshes.filter(o=>o.visible),false)[0];
    if(hit?.object)selectPart(hit.object);
  });
  $("loadLocalModel").addEventListener("click",()=>$("localModel").click());
  $("localModel").addEventListener("change",async event=>{
    const file=event.target.files&&event.target.files[0];
    if(!file)return;
    const extension=file.name.toLowerCase();
    if(file.size>80*1024*1024||!(extension.endsWith(".glb")||extension.endsWith(".3mf"))){
      status.textContent="Please select a 3MF or GLB smaller than 80 MB";return;
    }
    status.textContent="Parsing local CAD file (stays on this device)…";
    try{
      const array=await file.arrayBuffer();
      const loaded=assembly=>{
        setAssembly(assembly,true);
        mode.textContent="Local CAD loaded • never uploaded to a server";
        status.textContent="Local "+file.name+" · real CAD · "+meshes.length+" render meshes";
      };
      if(extension.endsWith(".3mf")){
        const loader=new ThreeMFLoader();
        const group=loader.parse(array);
        // 3MF can contain many thousands of parts; flatten only the scene graph,
        // preserving the geometry and original part transforms.
        loaded(group);
      }else{
        new GLTFLoader().parse(array,"",gltf=>loaded(gltf.scene),
          ()=>{status.textContent="The selected GLB is damaged or unsupported";});
      }
    }catch(err){status.textContent="Unable to read this local model";}
    event.target.value="";
  });
  $("explode").addEventListener("input",updateExplode);
  $("wireframeToggle").addEventListener("change",setWireframe);
  $("housingToggle").addEventListener("change",showHousing);
  controls.addEventListener("start",()=>{manualInput=true;transitioning=false;});
  controls.addEventListener("end",()=>{manualInput=false;});
  // Only original, owner-supplied CAD. No synthetic model is rendered.
  status.textContent="Loading the original supplied G1 CAD assembly…";
  mode.textContent="Hosted earlier CAD revision · use local upload for latest";
  // Prefer the build-generated, CAD-derived GLB for faster rendering on Pages.
  // In source previews where no GLB is generated, load the exact source 3MF.
  const loadedModel = model=>{setAssembly(model,true);setWireframe();showHousing();};
  const loadSource3MF = ()=>new ThreeMFLoader().load("./assets/g1-assembly.3mf",
    loadedModel, undefined,
    ()=>{status.textContent="Original CAD failed to load; choose Open your 3D CAD for a local preview";});
  new GLTFLoader().load("./assets/g1-assembly.glb",
    gltf=>loadedModel(gltf.scene), undefined, loadSource3MF);
  $("loadSourceModel").addEventListener("click",()=>{
    status.textContent="Loading all original hosted 3MF bodies (may take several seconds)…";
    loadSource3MF();
    mode.textContent="Full source geometry requested · previous revision";
  });

  function resize(){
    const width=Math.max(1,wrap.clientWidth),height=Math.max(1,wrap.clientHeight);
    renderer.setSize(width,height,false);
    camera.aspect=width/height;camera.updateProjectionMatrix();
  }
  const observer=new ResizeObserver(resize);observer.observe(wrap);resize();
  let last=0,active=true;
  const visibility=()=>{active=!document.hidden;};
  document.addEventListener("visibilitychange",visibility);
  function animate(now){
    requestAnimationFrame(animate);
    if(!active)return;
    const dt=Math.min((now-last)||16,70);last=now;
    if(transitioning){
      camera.position.lerp(targetCamera,.11);
      if(camera.position.distanceTo(targetCamera)<.3){camera.position.copy(targetCamera);transitioning=false;}
    }
    if(assembly&&$("rotateToggle").checked&&!reducedMotion&&!manualInput){
      assembly.rotation.z+=.00018*dt;
      assembly.rotation.x=-.08+Math.sin(now*.00023)*.07;
    }
    controls.update();
    renderer.render(scene,camera);
  }
  requestAnimationFrame(animate);
}
const $$=(q,root=document)=>[...root.querySelectorAll(q)];
