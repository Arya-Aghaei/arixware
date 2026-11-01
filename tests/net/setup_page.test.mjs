import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';

const html=readFileSync(new URL('../../components/net/setup.html',import.meta.url),'utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const tick=()=>new Promise(resolve=>setImmediate(resolve));
async function fixture(initialStatus, initialScan, ownerName = "Arya") {
  function element() {
    return {value:'',textContent:'',disabled:false,checked:false,children:[],dataset:{},attrs:{},classList:{toggle(){}},
      append(...items){this.children.push(...items)},replaceChildren(...items){this.children=items},focus(){},
      setAttribute(name,value){this.attrs[name]=value},
      click(){if(!this.disabled)return this.onclick?.()},set innerHTML(_){throw Error('Unsafe HTML assignment')}
    };
  }
  const ids=['status','connect','scan','ssid','pass','networks','show','setup','selection','owner'];
  const nodes=Object.fromEntries(ids.map(id=>[id,element()]));
  const timers=new Map();let next=0;
  const calls=[];
  let status=initialStatus||{state:'setup',error:'',setup_active:true};
  let scan=initialScan||{scanning:false,error:'',networks:[{ssid:'<img src=x onerror=alert(1)>',secured:true,rssi:-45}]};
  let broken=false;
  const context=vm.createContext({
    document:{getElementById:id=>nodes[id],createElement:()=>element()},
    Option:function(text,value){this.text=text;this.value=value},TextEncoder,
    setTimeout:fn=>{timers.set(++next,fn);return next},clearTimeout:id=>timers.delete(id),
    fetch:async(path,options={})=>{
      calls.push([path,options]);
      if(broken)throw Error('Network unavailable');
      let data=path==='/api/profile'?{owner_name:ownerName}:path==='/api/status'?status:path==='/api/wifi/scan'?scan:{ok:true};
      return {ok:true,json:async()=>data};
    }
  });
  vm.runInContext(script,context);
  await tick();await tick();
  return {nodes,calls,setStatus:s=>status=s,setScan:s=>scan=s,breakNetwork:()=>broken=true,setBroken:b=>broken=b,
    async timer(){const entry=timers.entries().next().value;assert(entry,'Expected polling timer');timers.delete(entry[0]);await entry[1]();await tick()}};
}
const missing=await fixture(undefined,undefined,'');
missing.nodes.ssid.value='Home';missing.nodes.pass.value='password123';
await missing.nodes.setup.onsubmit({preventDefault(){}});
assert.match(missing.nodes.status.textContent,/enter your name/);
assert(!missing.calls.some(([path])=>path==='/api/wifi'));
missing.nodes.owner.value='é'.repeat(33);
await missing.nodes.setup.onsubmit({preventDefault(){}});
assert.match(missing.nodes.status.textContent,/too long/);
missing.nodes.owner.value='First\nLast';
await missing.nodes.setup.onsubmit({preventDefault(){}});
assert.match(missing.nodes.status.textContent,/enter your name/);
const literal=await fixture(undefined,undefined,'<img src=x onerror=alert(1)>');
assert.equal(literal.nodes.owner.value,'<img src=x onerror=alert(1)>');
const f=await fixture();
assert.equal(f.nodes.owner.value,'Arya');
f.nodes.owner.value='  Arya  ';
assert.equal(f.nodes.networks.children[0].children[0].textContent,'<img src=x onerror=alert(1)>');
await f.nodes.networks.children[0].click();
assert.equal(f.nodes.ssid.value,'<img src=x onerror=alert(1)>');
assert.equal(f.nodes.networks.children[0].attrs['aria-pressed'],'true');
f.nodes.ssid.value='Home';f.nodes.ssid.oninput();
assert.equal(f.nodes.networks.children[0].attrs['aria-pressed'],'false');
f.nodes.pass.value='short';await f.nodes.setup.onsubmit({preventDefault(){}});
assert.match(f.nodes.status.textContent,/8–63/);
assert.equal(f.calls.filter(([path])=>path==='/api/wifi').length,0);
f.nodes.ssid.value='é'.repeat(17);f.nodes.pass.value='password123';
await f.nodes.setup.onsubmit({preventDefault(){}});assert.match(f.nodes.status.textContent,/32 bytes/);
f.nodes.ssid.value='Home';await f.nodes.setup.onsubmit({preventDefault(){}});
assert(f.nodes.connect.disabled);
assert(f.nodes.owner.disabled);
assert(f.nodes.networks.children.every(button=>button.disabled));
assert.deepEqual(JSON.parse(f.calls.find(([p])=>p==='/api/wifi')[1].body),{ssid:'Home',pass:'password123',owner_name:'Arya'});
f.setStatus({state:'setup',error:'Wrong password'});await f.timer();
assert.equal(f.nodes.connect.disabled,false);assert.equal(f.nodes.status.textContent,'Wrong password');
f.nodes.pass.value='correctpassword';await f.nodes.setup.onsubmit({preventDefault(){}});
f.setStatus({state:'connected',ssid:'Home',ip:'192.168.1.9',error:''});await f.timer();
assert.match(f.nodes.status.textContent,/Connected to Home/);assert.equal(f.nodes.pass.value,'');
assert.equal(f.nodes.connect.disabled,false);
const g=await fixture();g.nodes.ssid.value='Open';g.nodes.pass.value='';
g.breakNetwork();await g.nodes.setup.onsubmit({preventDefault(){}});
assert.equal(g.nodes.connect.disabled,false);assert.match(g.nodes.status.textContent,/Network unavailable/);
const reopening=await fixture({state:'connected',ssid:'Existing',ip:'192.168.1.9',error:'',setup_active:true});
assert.match(reopening.nodes.status.textContent,/Currently connected to Existing/);
assert(reopening.calls.some(([path,options])=>path==='/api/wifi/scan'&&options.method==='POST'));
assert.equal(reopening.nodes.networks.children.length,1);
assert.equal(reopening.nodes.connect.disabled,false);
const completed=await fixture({state:'connected',ssid:'Home',ip:'192.168.1.9',error:'',setup_active:true,setup_complete:true});
assert.match(completed.nodes.status.textContent,/Connected to Home/);
assert(!completed.calls.some(([path])=>path==='/api/wifi/scan'));
const scanning=await fixture(undefined,{scanning:true,error:'',networks:[]});
assert.equal(scanning.nodes.scan.disabled,true);
scanning.setScan({scanning:false,error:'',networks:[{ssid:'Home',secured:true},{ssid:'Guest',secured:false}]});
await scanning.timer();
assert.deepEqual(scanning.nodes.networks.children.map(n=>n.children[0].textContent),['Home','Guest']);
assert.match(scanning.nodes.networks.children[1].children[1].textContent,/Open network/);
await scanning.nodes.networks.children[1].click();
assert.equal(scanning.nodes.ssid.value,'Guest');
assert.equal(scanning.nodes.scan.disabled,false);
scanning.setScan({scanning:false,error:'Scan failed. Try again.',networks:[]});
await scanning.nodes.scan.click();
assert.match(scanning.nodes.status.textContent,/Scan failed/);
assert.equal(scanning.nodes.scan.disabled,false);

const transient=await fixture();
transient.nodes.ssid.value='Office';transient.nodes.pass.value='secretpass';
await transient.nodes.setup.onsubmit({preventDefault(){}});
assert(transient.nodes.connect.disabled);
transient.setBroken(true);
await transient.timer();
assert(transient.nodes.connect.disabled);
transient.setBroken(false);
transient.setStatus({state:'connected',ssid:'Office',ip:'192.168.1.50',error:''});
await transient.timer();
assert.equal(transient.nodes.connect.disabled,false);
assert.match(transient.nodes.status.textContent,/Connected to Office/);

const failed=await fixture();
failed.nodes.ssid.value='Office';failed.nodes.pass.value='secretpass';
await failed.nodes.setup.onsubmit({preventDefault(){}});
assert(failed.nodes.connect.disabled);
failed.setStatus({state:'setup',error:''});
await failed.timer();
assert.equal(failed.nodes.connect.disabled,false);
assert.match(failed.nodes.status.textContent,/Connection failed/);

console.log('Setup page: safe network labels, byte limits, retry, successful connection and network failure passed');
