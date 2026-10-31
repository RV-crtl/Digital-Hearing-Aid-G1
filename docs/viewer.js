/* G1 WebGL assembly explorer. Loads the authentic supplied 3MF CAD assembly.
   Any conceptual geometry is not used as the hosted model. */
import * as THREE from "three";
import {OrbitControls} from "three/addons/controls/OrbitControls.js";
import {GLTFLoader} from "three/addons/loaders/GLTFLoader.js";
import {ThreeMFLoader} from "three/addons/loaders/3MFLoader.js";

const $=id=>document.getElementById(id);
const colors={board:0x285e51,shell:0x354d5b,chip:0x26313f,copper:0xe5bf60,wire:0x88dfcc};
const MATERIALS={
  board:new THREE.MeshStandardMaterial({color:colors.board,metalness:.22,roughness:.59}),
  shell:new THREE.MeshPhysicalMaterial({color:colors.shell,metalness:.23,roughness:.33,transparent:true,opacity:.79,clearcoat:.45,side:THREE.DoubleSide,depthWrite:false}),
  chip:new THREE.MeshStandardMaterial({color:colors.chip,metalness:.2,roughness:.44}),
  copper:new THREE.MeshStandardMaterial({color:colors.copper,metalness:.73,roughness:.24,side:THREE.DoubleSide}),
  silkscreen:new THREE.MeshStandardMaterial({color:0xb1ead5,metalness:0,roughness:.85}),
  pin:new THREE.MeshStandardMaterial({color:0xbbc7d3,metalness:.75,roughness:.31})
};
function box(w,h,d,mat,x,y,z){
  const mesh=new THREE.Mesh(new THREE.BoxGeometry(w,h,d),mat);
  mesh.position.set(x,y,z);return mesh;
}
function roundedPlate(w,h,depth,radius,mat){
  const x=-w/2,y=-h/2;
  const sh=new THREE.Shape();
  sh.moveTo(x+radius,y);sh.lineTo(x+w-radius,y);sh.quadraticCurveTo(x+w,y,x+w,y+radius);
  sh.lineTo(x+w,y+h-radius);sh.quadraticCurveTo(x+w,y+h,x+w-radius,y+h);
  sh.lineTo(x+radius,y+h);sh.quadraticCurveTo(x,y+h,x,y+h-radius);
  sh.lineTo(x,y+radius);sh.quadraticCurveTo(x,y,x+radius,y);
  const mesh=new THREE.Mesh(new THREE.ExtrudeGeometry(sh,{depth,steps:1,bevelEnabled:true,bevelThickness:.8,bevelSize:.8,bevelSegments:3,curveSegments:8}),mat);
  return mesh;
}
function addTrace(group,points,z){
  const curve=new THREE.CatmullRomCurve3(points.map(p=>new THREE.Vector3(p[0],p[1],z)),false,"centripetal");
  const mesh=new THREE.Mesh(new THREE.TubeGeometry(curve,Math.max(8,points.length*4),.18,5,false),MATERIALS.copper);
  group.add(mesh);
}
function placeholderAssembly(){
  const root=new THREE.Group();root.name="ConceptualG1";
  const shell=new THREE.Group();shell.name="Shell";
  const board=new THREE.Group();board.name="PCB";
  const electronic=new THREE.Group();electronic.name="Components";
  const copper=new THREE.Group();copper.name="Copper";
  const contacts=new THREE.Group();contacts.name="Contacts";
  const base=roundedPlate(100,78,4,12,MATERIALS.shell);base.position.z=-13;shell.add(base);
  const rim=roundedPlate(100,78,5,12,new THREE.MeshPhysicalMaterial({color:0x587d88,transparent:true,opacity:.16,roughness:.25,side:THREE.DoubleSide,depthWrite:false}));rim.position.z=8;shell.add(rim);
  const pcb=roundedPlate(96,72,1.6,5,MATERIALS.board);pcb.position.z=-4;board.add(pcb);
  for(let i=0;i<4;i++)for(let j=0;j<2;j++)board.add(box(10,1.2,.2,MATERIALS.silkscreen,-31+i*21,31-j*62,-1.7));
  const chips=[[0,0,21,17,4],[-31,-14,14,12,4],[26,16,18,12,3],[31,-19,10,8,3],[-12,23,11,9,3]];
  chips.forEach(([x,y,w,h,z],i)=>{
    electronic.add(box(w,h,z,MATERIALS.chip,x,y,-1.2+z/2));
    const n= Math.floor(w/2.5);
    for(let pin=0;pin<n;pin++){
      const px=x-w/2+1.8+pin*2.5;
      contacts.add(box(1.2,3,.45,MATERIALS.pin,px,y-h/2-1.5,-.9));
      contacts.add(box(1.2,3,.45,MATERIALS.pin,px,y+h/2+1.5,-.9));
    }
  });
  for(let i=0;i<38;i++){
    const sx=Math.sin(i*14.15)*38,sy=Math.cos(i*6.73)*27;
    const ex=sx*.55+Math.cos(i*1.67)*16,ey=sy*.6;
    addTrace(copper,[[sx,sy],[sx,ey],[ex,ey]],-1.88);
  }
  for(let i=0;i<3;i++)electronic.add(box(24,7,5,MATERIALS.chip,-28+i*26,-28,-.8));
  electronic.add(box(14,12,12,MATERIALS.pin,36,28,4.5));
  for(let i=0;i<4;i++){
    const sx=i%2?-44:44,sy=i<2?-33:33;
    contacts.add(new THREE.Mesh(new THREE.CylinderGeometry(2.3,2.3,1.5,18),MATERIALS.pin));
    contacts.children[contacts.children.length-1].rotation.x=Math.PI/2;
    contacts.children[contacts.children.length-1].position.set(sx,sy,-.2);
  }
  root.add(shell,board,copper,electronic,contacts);
  return root;
}

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
  const startPos=new WeakMap();
  let currentView="iso";
  const presets={iso:[142,-168,142],top:[0,-4,230],side:[213,-18,56]};
  let transitioning=false;
  const targetCamera=new THREE.Vector3(...presets.iso);
  const status=$("modelStatus"),mode=$("modelMode");
  function setAssembly(next,isCAD){
    if(assembly)scene.remove(assembly);
    assembly=next;loadedCAD=isCAD;
    const bnd=new THREE.Box3().setFromObject(assembly);
    const center=bnd.getCenter(new THREE.Vector3());
    assembly.position.sub(center);
    assembly.rotation.set(-.08,0,-.12);
    assembly.traverse(obj=>{if(obj.isMesh){obj.castShadow=false;obj.receiveShadow=false;obj.material=obj.material.clone();}});
    for(const child of assembly.children){startPos.set(child,child.position.clone());}
    scene.add(assembly);
    updateExplode();
    status.textContent=isCAD?"Authentic supplied CAD assembly · drag to inspect":"CAD model unavailable";
    mode.textContent=isCAD?"CAD-derived model loaded • interact freely":"Conceptual fallback — not the uploaded CAD geometry";
    resetCamera();
  }
  function updateExplode(){
    if(!assembly)return;
    const amount=Number($("explode").value)/100;
    $("explodeValue").textContent=Math.round(amount*100)+"%";
    for(const node of assembly.children){
      const base=startPos.get(node)||new THREE.Vector3();
      const name=(node.name||"").toLowerCase();
      let displacement=0;
      if(name.includes("shell"))displacement=32;
      else if(name.includes("component")||name.includes("electronic"))displacement=14;
      else if(name.includes("contact"))displacement=23;
      else if(name.includes("copper"))displacement=3;
      node.position.copy(base);node.position.z+=amount*displacement;
    }
  }
  function setWireframe(){
    if(!assembly)return;
    const enabled=$("wireframeToggle").checked;
    assembly.traverse(obj=>{if(obj.isMesh&&obj.material){for(const mat of (Array.isArray(obj.material)?obj.material:[obj.material]))mat.wireframe=enabled;}});
  }
  function showHousing(){
    if(!assembly)return;
    const visible=$("housingToggle").checked;
    assembly.children.forEach(node=>{
      if(node.name.toLowerCase().includes("shell"))node.visible=visible;
    });
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
  $("resetModel").addEventListener("click",resetCamera);
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
        status.textContent="Local "+file.name+" · drag or explode to explore";
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
  // G11: never substitute a fictional device for the actual supplied CAD.
  // Until the approved GLB is hosted, show an empty stage with an explicit
  // local-load invitation. Native 3MF input is handled without upload.
  status.textContent="Loading the original supplied G1 CAD assembly…";
  mode.textContent="Authentic supplied 3MF — automatic hosted preview";
  new ThreeMFLoader().load("./assets/g1-assembly.3mf",
    model=>{setAssembly(model,true);setWireframe();showHousing();},
    undefined,
    ()=>{status.textContent="Hosted original 3MF could not load; choose Open your 3D CAD to view your local copy";});

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
