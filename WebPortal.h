// =============================================================================
// WebPortal.h — FiskeyPass v4.0.0 Dark-Themed Web Dashboard (PROGMEM)
// =============================================================================
//
// Complete captive portal HTML/CSS/JS as PROGMEM strings.
// No CDN dependencies — all CSS inline (works in captive portal without
// internet).
//
// Dark palette: #0d1117 bg, #161b22 cards, #58a6ff accent, #f0f6fc text
// 4 tabs: Dashboard | Vault | Import | Settings
//
// SECURITY MODEL (v4.0.0):
//   - WPA2-PSK protects the AP at the network level.
//   - A mandatory PIN unlock modal gates ALL dashboard access.
//   - The PIN is the user's physical 6-digit FiskeyPass PIN.
//   - Passwords are NEVER sent in HTTP GET responses.
//   - All API calls are plain HTTP/JSON — no ECDH or HTTPS.
// =============================================================================

#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Main Dashboard HTML — Stored in flash (PROGMEM)
// ─────────────────────────────────────────────────────────────────────────────

static const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FiskeyPass Portal</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;
background:#0d1117;color:#f0f6fc;min-height:100vh;}
.wrap{max-width:720px;margin:0 auto;padding:16px;}

/* Header */
.header{text-align:center;padding:20px 0 12px;border-bottom:1px solid #21262d;margin-bottom:16px;}
.header h1{font-size:24px;font-weight:700;color:#f0f6fc;letter-spacing:0.5px;}
.header h1 span{color:#58a6ff;}
.header .ver{color:#8b949e;font-size:12px;margin-top:4px;}

/* Tabs */
.tabs{display:flex;gap:4px;margin-bottom:16px;border-bottom:1px solid #21262d;padding-bottom:0;}
.tab{padding:10px 16px;cursor:pointer;color:#8b949e;font-size:14px;font-weight:500;
border:none;background:none;border-bottom:2px solid transparent;transition:all 0.2s;}
.tab:hover{color:#f0f6fc;}
.tab.active{color:#58a6ff;border-bottom-color:#58a6ff;}

/* Cards */
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:20px;margin-bottom:16px;}
.card h2{font-size:16px;color:#f0f6fc;margin-bottom:14px;padding-bottom:8px;border-bottom:1px solid #21262d;}
.card h3{font-size:14px;color:#c9d1d9;margin:12px 0 8px;}

/* Tab Panels */
.panel{display:none;}
.panel.active{display:block;}

/* Forms */
label{display:block;margin:10px 0 4px;font-size:13px;color:#8b949e;font-weight:500;}
input[type="text"],input[type="password"],input[type="number"]{
width:100%;padding:10px 12px;background:#0d1117;border:1px solid #30363d;border-radius:6px;
color:#f0f6fc;font-size:14px;outline:none;transition:border 0.2s;}
input:focus{border-color:#58a6ff;}
input::placeholder{color:#484f58;}

/* Buttons */
.btn{display:inline-flex;align-items:center;gap:6px;padding:8px 16px;border:none;border-radius:6px;
font-size:13px;font-weight:600;cursor:pointer;transition:all 0.15s;text-decoration:none;}
.btn-primary{background:#238636;color:#fff;}
.btn-primary:hover{background:#2ea043;}
.btn-accent{background:#1f6feb;color:#fff;}
.btn-accent:hover{background:#388bfd;}
.btn-danger{background:#da3633;color:#fff;}
.btn-danger:hover{background:#f85149;}
.btn-ghost{background:transparent;color:#8b949e;border:1px solid #30363d;}
.btn-ghost:hover{background:#21262d;color:#f0f6fc;}
.btn-sm{padding:5px 10px;font-size:12px;}
.actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:14px;}

/* Table */
table{width:100%;border-collapse:collapse;margin-top:10px;}
th,td{padding:10px 12px;text-align:left;border-bottom:1px solid #21262d;font-size:13px;}
th{color:#8b949e;font-weight:600;font-size:12px;text-transform:uppercase;letter-spacing:0.5px;}
td{color:#c9d1d9;}
tr:hover td{background:#1c2128;}
.td-actions{display:flex;gap:6px;}

/* Status badges */
.badge{display:inline-block;padding:3px 8px;border-radius:12px;font-size:11px;font-weight:600;}
.badge-green{background:#0d411e;color:#3fb950;}
.badge-yellow{background:#3d2e00;color:#d29922;}
.badge-blue{background:#0c2d6b;color:#58a6ff;}

/* Progress bar */
.progress-wrap{background:#21262d;border-radius:4px;height:8px;margin:8px 0;overflow:hidden;}
.progress-bar{height:100%;background:linear-gradient(90deg,#238636,#3fb950);border-radius:4px;transition:width 0.5s;}

/* Stats grid */
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px;margin-bottom:16px;}
.stat{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:14px;text-align:center;}
.stat .val{font-size:24px;font-weight:700;color:#58a6ff;}
.stat .lbl{font-size:12px;color:#8b949e;margin-top:4px;}

/* Drop zone */
.dropzone{border:2px dashed #30363d;border-radius:8px;padding:32px;text-align:center;
color:#8b949e;cursor:pointer;transition:all 0.2s;margin-top:10px;}
.dropzone:hover,.dropzone.dragover{border-color:#58a6ff;background:#0d1117;color:#58a6ff;}
.dropzone input{display:none;}
.dropzone .icon{font-size:32px;margin-bottom:8px;}

/* Toast */
.toast{position:fixed;bottom:20px;right:20px;padding:12px 20px;border-radius:8px;
font-size:13px;font-weight:500;transform:translateY(100px);opacity:0;
transition:all 0.3s;z-index:1000;max-width:320px;}
.toast.show{transform:translateY(0);opacity:1;}
.toast-ok{background:#0d411e;color:#3fb950;border:1px solid #238636;}
.toast-err{background:#3d0c0c;color:#f85149;border:1px solid #da3633;}

/* Modal */
.modal-bg{display:none;position:fixed;top:0;left:0;width:100%;height:100%;
background:rgba(0,0,0,0.6);z-index:500;align-items:center;justify-content:center;}
.modal-bg.show{display:flex;}
.modal{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:24px;
max-width:400px;width:90%;text-align:center;}
.modal h3{margin-bottom:12px;color:#f85149;}
.modal p{color:#8b949e;font-size:14px;margin-bottom:18px;}

/* PIN unlock modal — full opaque background */
#pin-modal{z-index:9999;background:#0d1117;}
#pin-modal .modal h3{color:#58a6ff;}
#pin-modal .pin-icon{font-size:48px;margin-bottom:12px;}
#pin-modal input{text-align:center;font-size:24px;letter-spacing:12px;max-width:200px;margin:0 auto;}
#pin-err{color:#f85149;font-size:13px;margin-top:10px;min-height:18px;}

/* Responsive */
@media(max-width:500px){
.tabs{overflow-x:auto;}
.tab{padding:8px 12px;font-size:13px;white-space:nowrap;}
.stats{grid-template-columns:1fr 1fr;}
}
</style>
</head>
<body>
<div class="wrap">

<!-- Header -->
<div class="header">
<h1>Fiskey<span>Pass</span></h1>
<div class="ver">v4.0.0 &middot; Secure Portal</div>
</div>

<!-- Tabs -->
<div class="tabs">
<button class="tab active" onclick="showTab('dashboard')">Dashboard</button>
<button class="tab" onclick="showTab('vault')">Vault</button>
<button class="tab" onclick="showTab('import')">Import</button>
<button class="tab" onclick="showTab('settings')">Settings</button>
</div>

<!-- ═══════════════ DASHBOARD ═══════════════ -->
<div id="tab-dashboard" class="panel active">
<div class="stats">
<div class="stat"><div class="val" id="st-entries">-</div><div class="lbl">Entries</div></div>
<div class="stat"><div class="val" id="st-storage">-</div><div class="lbl">Storage Used</div></div>
<div class="stat"><div class="val" id="st-free">-</div><div class="lbl">Free Space</div></div>
<div class="stat"><div class="val" id="st-max">24</div><div class="lbl">Max Entries</div></div>
</div>
<div class="card">
<h2>Quick Actions</h2>
<div class="actions">
<button class="btn btn-accent" onclick="showTab('vault')">&#128274; Manage Vault</button>
<button class="btn btn-accent" onclick="showTab('import')">&#128228; Import Data</button>
<button class="btn btn-ghost" onclick="showTab('settings')">&#9881; Settings</button>
</div>
</div>
</div>

<!-- ═══════════════ VAULT ═══════════════ -->
<div id="tab-vault" class="panel">
<div class="card">
<h2>Add Entry</h2>
<label>Name</label>
<input type="text" id="add-name" placeholder="e.g. Gmail" maxlength="31">
<label>Username</label>
<input type="text" id="add-user" placeholder="e.g. user@gmail.com" maxlength="63">
<label>Password</label>
<input type="password" id="add-pass" placeholder="Enter password" maxlength="63">
<div style="margin-top:6px;">
<button class="btn btn-ghost btn-sm" onclick="togglePwdVis('add-pass',this)">Show</button>
<button class="btn btn-ghost btn-sm" onclick="genPwd()">Generate</button>
</div>
<div class="actions">
<button class="btn btn-primary" onclick="addEntry()">&#10010; Add Entry</button>
</div>
</div>
<div class="card">
<h2>Stored Credentials</h2>
<div id="vault-list"><p style="color:#8b949e">Loading...</p></div>
</div>
</div>

<!-- ═══════════════ IMPORT ═══════════════ -->
<div id="tab-import" class="panel">
<div class="card">
<h2>Import Credentials</h2>
<p style="color:#8b949e;font-size:13px;margin-bottom:12px;">
Upload a CSV file (Name,Username,Password) or KeePass XML export.
Entries will be appended to your existing vault.
</p>
<div class="dropzone" id="dropzone" onclick="document.getElementById('file-input').click()">
<div class="icon">&#128196;</div>
<div>Drop file here or <strong style="color:#58a6ff">browse</strong></div>
<div style="font-size:12px;margin-top:4px;">Supports .csv and .xml</div>
<input type="file" id="file-input" accept=".csv,.xml">
</div>
<div id="file-info" style="margin-top:10px;display:none;">
<span class="badge badge-blue" id="file-name"></span>
<span style="color:#8b949e;font-size:12px;margin-left:6px;" id="file-size"></span>
</div>
<div class="actions">
<button class="btn btn-primary" id="import-btn" onclick="doImport()" disabled>&#128228; Import</button>
</div>
</div>
</div>

<!-- ═══════════════ SETTINGS ═══════════════ -->
<div id="tab-settings" class="panel">
<div class="card">
<h2>Storage</h2>
<div style="display:flex;justify-content:space-between;font-size:13px;color:#8b949e;">
<span>Used: <strong id="set-used" style="color:#c9d1d9">-</strong></span>
<span>Total: <strong id="set-total" style="color:#c9d1d9">-</strong></span>
</div>
<div class="progress-wrap"><div class="progress-bar" id="set-bar" style="width:0%"></div></div>
<div style="text-align:right;font-size:12px;color:#8b949e;" id="set-pct">-</div>
</div>
<div class="card">
<h2>Change PIN</h2>
<label>Current PIN</label>
<input type="password" id="pin-old" placeholder="Enter current 6-digit PIN" maxlength="6">
<label>New PIN</label>
<input type="password" id="pin-new" placeholder="Enter new 6-digit PIN" maxlength="6">
<label>Confirm New PIN</label>
<input type="password" id="pin-conf" placeholder="Repeat new PIN" maxlength="6">
<div class="actions">
<button class="btn btn-accent" onclick="changePin()">Change PIN</button>
</div>
</div>
<div class="card">
<h2 style="color:#f85149;">Danger Zone</h2>
<p style="color:#8b949e;font-size:13px;margin-bottom:12px;">
Factory reset will erase ALL stored credentials and settings. This cannot be undone.
</p>
<button class="btn btn-danger" onclick="showResetModal()">&#9888; Factory Reset</button>
</div>
</div>

</div><!-- /wrap -->

<!-- PIN Unlock Modal (mandatory — blocks all access until correct PIN) -->
<div class="modal-bg show" id="pin-modal">
<div class="modal">
<div class="pin-icon">&#128274;</div>
<h3>Vault Locked</h3>
<p>Enter your FiskeyPass physical PIN to unlock the vault.</p>
<input type="password" id="pin-unlock" placeholder="------" maxlength="6"
       inputmode="numeric" pattern="[0-9]*" autocomplete="off">
<div class="actions" style="justify-content:center;margin-top:14px;">
<button class="btn btn-primary" id="pin-unlock-btn" onclick="doUnlock()" style="width:100%;max-width:200px;">Unlock Vault</button>
</div>
<div id="pin-err"></div>
</div>
</div>

<!-- Reset Confirmation Modal -->
<div class="modal-bg" id="reset-modal">
<div class="modal">
<h3>&#9888; Factory Reset</h3>
<p>This will permanently delete all credentials, PIN, and settings. The device will reboot.</p>
<div class="actions" style="justify-content:center;">
<button class="btn btn-danger" onclick="doReset()">Yes, Erase Everything</button>
<button class="btn btn-ghost" onclick="hideModal()">Cancel</button>
</div>
</div>
</div>

<!-- Toast -->
<div class="toast" id="toast"></div>

<script>
/* ─── PIN Unlock ─── */
document.getElementById('pin-unlock').addEventListener('keydown',function(e){
  if(e.key==='Enter') doUnlock();
});

function doUnlock(){
  var pin=document.getElementById('pin-unlock').value.trim();
  var err=document.getElementById('pin-err');
  var btn=document.getElementById('pin-unlock-btn');
  if(pin.length!==6||!/^\d{6}$/.test(pin)){err.textContent='PIN must be exactly 6 digits';return;}
  err.textContent='';
  btn.disabled=true; btn.textContent='Unlocking...';
  fetch('/api/vault-unlock',{
    method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({pin:pin})
  }).then(function(r){return r.json();}).then(function(d){
    if(d.ok){
      document.getElementById('pin-modal').classList.remove('show');
      toast('Vault unlocked — '+d.count+' entries loaded',true);
      loadVault(); loadStorage();
    } else {
      err.textContent=d.error||'Wrong PIN';
      btn.disabled=false; btn.textContent='Unlock Vault';
    }
  }).catch(function(){
    err.textContent='Network error';
    btn.disabled=false; btn.textContent='Unlock Vault';
  });
}

/* ─── Tab switching ─── */
function showTab(id){
  if(document.getElementById('pin-modal').classList.contains('show')){return;}
  document.querySelectorAll('.panel').forEach(function(p){p.classList.remove('active');});
  document.querySelectorAll('.tab').forEach(function(t){t.classList.remove('active');});
  document.getElementById('tab-'+id).classList.add('active');
  var tabs=['dashboard','vault','import','settings'];
  document.querySelectorAll('.tab')[tabs.indexOf(id)].classList.add('active');
  if(id==='dashboard'||id==='settings')loadStorage();
  if(id==='vault'||id==='dashboard')loadVault();
}

/* ─── Toast ─── */
function toast(msg,ok){
  var t=document.getElementById('toast');
  t.textContent=msg;
  t.className='toast '+(ok?'toast-ok':'toast-err')+' show';
  setTimeout(function(){t.classList.remove('show');},3000);
}

/* ─── Load vault list ─── */
function loadVault(){
  if(document.getElementById('pin-modal').classList.contains('show')) return;
  fetch('/api/vault').then(function(r){return r.json();}).then(function(data){
    var entries=data.entries||[];
    document.getElementById('st-entries').textContent=entries.length;
    var el=document.getElementById('vault-list');
    if(entries.length===0){el.innerHTML='<p style="color:#8b949e">No entries stored yet.</p>';return;}
    var h='<table><tr><th>#</th><th>Name</th><th>Username</th><th>Actions</th></tr>';
    for(var i=0;i<entries.length;i++){
      h+='<tr><td>'+(i+1)+'</td><td>'+esc(entries[i].n)+'</td><td>'+esc(entries[i].u)+'</td>';
      h+='<td class="td-actions"><button class="btn btn-ghost btn-sm" onclick="editEntry('+i+')">Edit</button>';
      h+='<button class="btn btn-danger btn-sm" onclick="delEntry('+i+')">Del</button></td></tr>';
    }
    h+='</table>';
    el.innerHTML=h;
  }).catch(function(){toast('Failed to load vault',false);});
}

/* ─── Add entry ─── */
function addEntry(){
  var n=document.getElementById('add-name').value.trim();
  var u=document.getElementById('add-user').value.trim();
  var p=document.getElementById('add-pass').value;
  if(!n){toast('Name is required',false);return;}
  if(!p){toast('Password is required',false);return;}
  fetch('/api/entry',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({n:n,u:u,p:p})
  }).then(function(r){return r.json();}).then(function(d){
    if(d.ok){toast('Entry added!',true);document.getElementById('add-name').value='';
    document.getElementById('add-user').value='';document.getElementById('add-pass').value='';loadVault();}
    else toast(d.error||'Failed',false);
  }).catch(function(){toast('Network error',false);});
}

/* ─── Edit entry ─── */
function editEntry(id){
  var n=prompt('New name (leave empty to keep):');
  var u=prompt('New username (leave empty to keep):');
  var p=prompt('New password (leave empty to keep):');
  if(n===null)return;
  fetch('/api/entry',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({id:id,n:n,u:u,p:p})
  }).then(function(r){return r.json();}).then(function(d){
    if(d.ok){toast('Entry updated!',true);loadVault();}
    else toast(d.error||'Failed',false);
  }).catch(function(){toast('Network error',false);});
}

/* ─── Delete entry ─── */
function delEntry(id){
  if(!confirm('Delete this entry?'))return;
  fetch('/api/entry?id='+id,{method:'DELETE'}).then(function(r){return r.json();}).then(function(d){
    if(d.ok){toast('Entry deleted',true);loadVault();}
    else toast(d.error||'Failed',false);
  }).catch(function(){toast('Network error',false);});
}

/* ─── Storage info ─── */
function loadStorage(){
  if(document.getElementById('pin-modal').classList.contains('show')) return;
  fetch('/api/storage').then(function(r){return r.json();}).then(function(d){
    var pct=Math.round((d.used/d.total)*100);
    document.getElementById('st-storage').textContent=fmtBytes(d.used);
    document.getElementById('st-free').textContent=fmtBytes(d.total-d.used);
    document.getElementById('set-used').textContent=fmtBytes(d.used);
    document.getElementById('set-total').textContent=fmtBytes(d.total);
    document.getElementById('set-bar').style.width=pct+'%';
    document.getElementById('set-pct').textContent=pct+'% used';
  }).catch(function(){});
}

function fmtBytes(b){if(b<1024)return b+'B';if(b<1048576)return (b/1024).toFixed(1)+'KB';return (b/1048576).toFixed(1)+'MB';}

/* ─── Import ─── */
var selectedFile=null;
document.getElementById('file-input').addEventListener('change',function(e){
  if(e.target.files.length>0)selectFile(e.target.files[0]);
});
var dz=document.getElementById('dropzone');
dz.addEventListener('dragover',function(e){e.preventDefault();dz.classList.add('dragover');});
dz.addEventListener('dragleave',function(){dz.classList.remove('dragover');});
dz.addEventListener('drop',function(e){e.preventDefault();dz.classList.remove('dragover');
  if(e.dataTransfer.files.length>0)selectFile(e.dataTransfer.files[0]);
});
function selectFile(f){
  var ext=f.name.split('.').pop().toLowerCase();
  if(ext!=='csv'&&ext!=='xml'){toast('Only .csv and .xml files supported',false);return;}
  selectedFile=f;
  document.getElementById('file-name').textContent=f.name;
  document.getElementById('file-size').textContent=fmtBytes(f.size);
  document.getElementById('file-info').style.display='block';
  document.getElementById('import-btn').disabled=false;
}
function doImport(){
  if(!selectedFile)return;
  var fd=new FormData();
  fd.append('file',selectedFile);
  document.getElementById('import-btn').disabled=true;
  document.getElementById('import-btn').textContent='Importing...';
  fetch('/api/import',{method:'POST',body:fd}).then(function(r){return r.json();}).then(function(d){
    if(d.ok){toast('Imported '+d.count+' entries!',true);loadVault();selectedFile=null;
    document.getElementById('file-info').style.display='none';}
    else toast(d.error||'Import failed',false);
    document.getElementById('import-btn').disabled=false;
    document.getElementById('import-btn').innerHTML='&#128228; Import';
  }).catch(function(){toast('Network error',false);
    document.getElementById('import-btn').disabled=false;
    document.getElementById('import-btn').innerHTML='&#128228; Import';
  });
}

/* ─── PIN Change ─── */
function changePin(){
  var o=document.getElementById('pin-old').value;
  var n=document.getElementById('pin-new').value;
  var c=document.getElementById('pin-conf').value;
  if(o.length!==6||n.length!==6){toast('PIN must be 6 digits',false);return;}
  if(n!==c){toast('New PINs do not match',false);return;}
  fetch('/api/pin',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({old:o,"new":n})
  }).then(function(r){return r.json();}).then(function(d){
    if(d.ok){toast('PIN changed successfully!',true);
    document.getElementById('pin-old').value='';
    document.getElementById('pin-new').value='';
    document.getElementById('pin-conf').value='';}
    else toast(d.error||'Failed',false);
  }).catch(function(){toast('Network error',false);});
}

/* ─── Factory Reset ─── */
function showResetModal(){document.getElementById('reset-modal').classList.add('show');}
function hideModal(){document.getElementById('reset-modal').classList.remove('show');}
function doReset(){
  fetch('/api/reset',{method:'POST'}).then(function(r){return r.json();}).then(function(d){
    if(d.ok){toast('Device reset. Rebooting...',true);hideModal();
    setTimeout(function(){location.reload();},3000);}
    else toast(d.error||'Failed',false);
  }).catch(function(){toast('Network error',false);});
}

/* ─── Helpers ─── */
function esc(s){if(!s)return'';var d=document.createElement('div');d.textContent=s;return d.innerHTML;}
function togglePwdVis(id,btn){var i=document.getElementById(id);
  if(i.type==='password'){i.type='text';btn.textContent='Hide';}
  else{i.type='password';btn.textContent='Show';}}
function genPwd(){var c='ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%^&*';
  var p='';for(var i=0;i<16;i++)p+=c[Math.floor(Math.random()*c.length)];
  document.getElementById('add-pass').value=p;document.getElementById('add-pass').type='text';
  toast('Password generated',true);}
</script>
</body>
</html>
)rawliteral";

#endif // WEB_PORTAL_H
