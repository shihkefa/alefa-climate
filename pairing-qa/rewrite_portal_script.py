from pathlib import Path

path = Path(__file__).parents[1] / "universal_c6_components" / "pairing_portal" / "portal.html"
text = path.read_text(encoding="utf-8")
start = text.index("<script>\n") + len("<script>\n")
end = text.index("\n</script>", start)
script = r"""(()=>{'use strict';
const $=id=>document.getElementById(id);
let config=null,lastOpen=false,busy=false,toastTimer;
function toast(text){$('toast').textContent=text;$('toast').hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').hidden=true,3000)}
async function copy(text){if(!text)return;try{if(navigator.clipboard&&window.isSecureContext)await navigator.clipboard.writeText(text);else{const t=document.createElement('textarea');t.value=text;t.style.position='fixed';t.style.opacity='0';document.body.append(t);t.select();if(!document.execCommand('copy'))throw Error();t.remove()}toast('已複製')}catch(e){toast('無法自動複製，請選取文字後複製')}}
function toggle(input,button){const show=$(input).type==='password';$(input).type=show?'text':'password';$(button).textContent=show?'隱藏':'顯示';$(button).setAttribute('aria-pressed',String(show))}
async function json(path,options){const r=await fetch(path,{cache:'no-store',...options});if(!r.ok){const text=await r.text();throw Error(r.status===403?'設定未開放；如需重新設定，請長按 BOOT 約 10 秒清除配對後重試。':text||'裝置暫時無法回應')}return r.json()}
function setConfig(c){config=c;$('local-url').textContent=c.url;$('local-url').href=c.url;$('copy-url').disabled=false;$('manual-code').textContent=c.manual_code}
function renderEnvironment(data,offline=false){if(data)$('return-air-card').hidden=data.type!=='air_conditioner';const t=data?.return_air_temperature,valid=!offline&&data?.online&&Number.isFinite(t);$('sensor-temperature').textContent=valid?t.toFixed(1):'—';$('sensor-pill').classList.toggle('good',valid);$('sensor-status').textContent=offline||data?.online===false?'連線中斷':valid?'空調已回報':'等待空調回報'}
function renderAppliance(a){if(!a)return;const labels={reset_required:'需要重設配對',starting:'準備中',detecting:'辨識中',connected:'已連線',no_response:'未收到回應',profile_mismatch:'設備已更換',identified_restarting:'辨識完成，重新啟動中',uart_error:'通訊埠異常',storage_error:'儲存失敗'};$('appliance-title').textContent=a.type==='dehumidifier'?'除濕機':a.type==='air_conditioner'?'空調':'正在辨識設備';$('appliance-status').textContent=labels[a.status]||a.status;$('appliance-pill').classList.toggle('good',a.online);$('appliance-info').textContent=[a.protocol,a.brand||'品牌未回報',a.model||'型號未回報'].join(' · ');$('appliance-note').textContent=(a.status==='profile_mismatch'||a.status==='reset_required')?'設備類型變更或保留了舊版配對。請先在原平台移除裝置，再按住 BOOT 10 秒重設及重新配對。':a.online?'已完成通訊握手。可繼續 Matter 配對或連接 Wi-Fi。':'請確認通訊線與設備電源。未通過辨識時不會送出控制指令。'}
function serviceValue(a,id){const v=a?.services?.[String(id)];return v==null?null:Number(v)}
function markGroup(root,value){if(!root)return;root.querySelectorAll('[data-fan]').forEach(b=>b.classList.toggle('active',Number(b.dataset.fan)===Number(value)))}
function markServices(root,a,ids){if(!root)return;ids.forEach(id=>{const v=serviceValue(a,id);root.querySelectorAll('[data-service="'+id+'"]').forEach(b=>b.classList.toggle('active',v!==null&&v!==0))})}
function renderControls(a){
  const visible=!!a&&a.online&&(a.type==='air_conditioner'||a.type==='dehumidifier');
  $('device-control').hidden=!visible;if(!visible)return;
  const ac=a.type==='air_conditioner';$('ac-controls').hidden=!ac;$('dh-controls').hidden=ac;
  $('device-control-title').textContent=ac?'空調控制':'除濕機控制';
  $('device-control-caption').textContent=ac?'空調裝置與 TaiSEIA 服務實體':'除濕機裝置與 TaiSEIA 服務實體';
  const root=$(ac?'ac-controls':'dh-controls');
  const power=$(ac?'ac-power':'dh-power');power.classList.toggle('active',!!a.power);power.textContent=a.power?'開啟':'關閉';
  markGroup($(ac?'ac-fan':'dh-fan'),ac?(a.speed===0?0:a.speed<=2?1:a.speed===3?2:3):a.speed);
  if(ac){
    $('ac-mode').value=String(a.mode);
    const target=Number(a.target);if(Number.isFinite(target)&&target>=16&&target<=30){$('ac-target').value=String(Math.round(target));$('ac-target-value').textContent=Math.round(target)+' °C'}
    $('ac-vertical').classList.toggle('active',serviceValue(a,14)>0);$('ac-horizontal').classList.toggle('active',serviceValue(a,16)>0);
    [6,11,12].forEach(id=>{const v=serviceValue(a,id);const input=root.querySelector('[data-service="'+id+'"]');if(v!==null&&input)input.value=String(v)});
    markServices(root,a,[5,8,23,24,26,27,30,32,57]);
  }else{
    const mode=serviceValue(a,1);if(mode!==null)$('dh-mode').value=String(mode);
    [3,4].forEach(id=>{const v=serviceValue(a,id);const input=root.querySelector('[data-service="'+id+'"]');if(v!==null&&input)input.value=String(v);const out=$(id===3?'dh-humidity-value':'dh-intensity-value');if(v!==null)out.textContent=String(v)+(id===3?' %':'')});
    markServices(root,a,[8,19,20,22,24,41]);
  }
}
async function sendControl(command,value,service=-1){
  if(!config||busy)return;
  const body=new URLSearchParams({token:config.token,command,value:String(value)});if(service>=0)body.set('service',String(service));
  try{await json('/control',{method:'POST',body});$('control-result').className='notice success';$('control-result').textContent='指令已送出，等待設備回報。'}catch(e){$('control-result').className='notice error';$('control-result').textContent=e.message}
  $('control-result').hidden=false;setTimeout(refresh,350);setTimeout(()=>{$('control-result').hidden=true},2500)
}
function render(s){renderAppliance(s.appliance);renderControls(s.appliance);$('wifi-status').textContent='Wi-Fi：'+(s.wifi?'已連線':'尚未連線');$('matter-status').textContent='Matter：'+(s.paired?'已配對':s.open?'可配對':'尚未配對');$('wifi-pill').classList.toggle('good',s.wifi);$('matter-pill').classList.toggle('good',s.paired);$('wifi-lock').hidden=!s.paired||s.setup;$('wifi-fields').disabled=busy||!config||(s.paired&&!s.setup);$('reopen-pairing').hidden=s.paired||s.open||!s.ready;$('qr-block').hidden=!s.open;$('pair-state').hidden=s.open;$('pair-hint').hidden=s.paired;
if(s.open){if(!lastOpen)$('qr-image').src='/matter/qr.svg?t='+Date.now()}else{$('qr-image').removeAttribute('src');$('manual-code').textContent='';$('pair-icon').classList.toggle('loading',!s.ready);$('pair-icon').textContent=s.paired?'✓':s.ready?'↻':'◎';$('pair-title').textContent=s.paired?'已加入 Matter 網路':s.ready?'配對時間已結束':'等待設備辨識與配對服務';$('pair-copy').textContent=s.paired?'Thread 配對已完成，Wi-Fi 已停用。若要重新設定，請長按 BOOT 約 10 秒清除 Wi-Fi 與 Matter 配對。':s.ready?'重新開啟配對，即可顯示這台裝置的 QR Code。':'接通設備電源後會先辨識通訊協定；辨識成功後自動啟動配對。'}lastOpen=s.open}
async function refresh(){try{const s=await json('/status');if(!config||(s.open&&!lastOpen))setConfig(await json('/config'));render(s);renderEnvironment(s.appliance);$('connection-note').hidden=true}catch(e){renderEnvironment(null,true);$('connection-note').hidden=false;$('connection-note').textContent='目前無法連到裝置。若已儲存 Wi-Fi，請接回家中網路，再開啟下方 .local 網址。'}}
$('show-password').onclick=()=>toggle('psk','show-password');$('copy-url').onclick=()=>copy(config?.url);$('copy-manual').onclick=()=>copy(config?.manual_code);
$('wifi-form').onsubmit=async e=>{e.preventDefault();if(!config||busy)return;const ssid=$('ssid').value,psk=$('psk').value;const bytes=v=>new TextEncoder().encode(v).length;if(bytes(ssid)>32||bytes(psk)>63||(psk&&bytes(psk)<8)){toast('名稱最多 32 bytes；密碼為 8–63 bytes。');return}busy=true;$('wifi-fields').disabled=true;$('save-wifi').textContent='正在儲存…';const body=new URLSearchParams({token:config.token,ssid,psk});try{await json('/wifi',{method:'POST',body});$('psk').value='';$('wifi-result').className='notice success';$('wifi-result').textContent='設定已儲存。連線成功後熱點會關閉；Thread 配對完成後 Wi-Fi 會停用。若要重新配網，請長按 BOOT 約 10 秒清除配對後重新設定。'}catch(e){$('wifi-result').className='notice error';$('wifi-result').textContent=e.message}finally{busy=false;$('wifi-result').hidden=false;$('save-wifi').textContent='儲存並連線 →';refresh()}};
$('reopen-pairing').onclick=async()=>{if(!config)return;$('reopen-pairing').disabled=true;try{await json('/pairing',{method:'POST',body:new URLSearchParams({token:config.token})});toast('正在重新開啟配對');setTimeout(refresh,700)}catch(e){toast(e.message)}finally{$('reopen-pairing').disabled=false}};
$('ac-power').onclick=()=>sendControl('power',$('ac-power').classList.contains('active')?0:1);$('dh-power').onclick=()=>sendControl('power',$('dh-power').classList.contains('active')?0:1);
$('ac-mode').onchange=()=>sendControl('mode',Number($('ac-mode').value));$('ac-target').oninput=()=>{$('ac-target-value').textContent=$('ac-target').value+' °C'};$('ac-target').onchange=()=>sendControl('target',Number($('ac-target').value));
$('dh-mode').onchange=()=>sendControl('service',Number($('dh-mode').value),1);$('dh-humidity').oninput=()=>{$('dh-humidity-value').textContent=$('dh-humidity').value+' %'};$('dh-humidity').onchange=()=>sendControl('service',Number($('dh-humidity').value),3);$('dh-intensity').oninput=()=>{$('dh-intensity-value').textContent=$('dh-intensity').value};$('dh-intensity').onchange=()=>sendControl('service',Number($('dh-intensity').value),4);
$('ac-fan').querySelectorAll('[data-fan]').forEach(b=>b.onclick=()=>sendControl('fan',Number(b.dataset.fan)));$('dh-fan').querySelectorAll('[data-fan]').forEach(b=>b.onclick=()=>sendControl('fan',Number(b.dataset.fan)));
$('ac-vertical').onclick=()=>sendControl('vertical',$('ac-vertical').classList.contains('active')?0:1);$('ac-horizontal').onclick=()=>sendControl('horizontal',$('ac-horizontal').classList.contains('active')?0:1);
document.querySelectorAll('#ac-controls button[data-service],#dh-controls button[data-service]').forEach(b=>b.onclick=()=>sendControl('service',b.classList.contains('active')?0:1,Number(b.dataset.service)));
document.querySelectorAll('input[type=range][data-service]').forEach(i=>i.onchange=()=>sendControl('service',Number(i.value),Number(i.dataset.service)));
refresh();setInterval(refresh,3000);
})()
"""
path.write_text(text[:start] + script + text[end:], encoding="utf-8")
