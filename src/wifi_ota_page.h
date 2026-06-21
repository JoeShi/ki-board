#ifndef WIFI_OTA_PAGE_H
#define WIFI_OTA_PAGE_H

#include <Arduino.h>

static const char OTA_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Kiro OTA Update</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#1a1a2e;color:#e0e0e0;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
.container{background:#16213e;border-radius:16px;padding:32px;max-width:420px;width:100%;box-shadow:0 8px 32px rgba(0,0,0,0.3)}
h1{font-size:1.4em;text-align:center;margin-bottom:4px;color:#fff}
.version{text-align:center;color:#888;font-size:0.85em;margin-bottom:24px}
.upload-area{border:2px dashed #444;border-radius:12px;padding:24px;text-align:center;margin-bottom:16px;transition:border-color 0.3s}
.upload-area.dragover{border-color:#4facfe}
.file-input{display:none}
.file-label{display:inline-block;padding:10px 24px;background:#4facfe;color:#fff;border-radius:8px;cursor:pointer;font-weight:500;transition:background 0.3s}
.file-label:hover{background:#00c6ff}
.file-name{margin-top:8px;font-size:0.9em;color:#aaa;word-break:break-all}
.btn{display:block;width:100%;padding:14px;border:none;border-radius:8px;background:#00c853;color:#fff;font-size:1em;font-weight:600;cursor:pointer;margin-top:16px;transition:background 0.3s}
.btn:hover{background:#00e676}
.btn:disabled{background:#555;cursor:not-allowed}
.progress-wrap{margin-top:16px;display:none}
.progress-bar{height:8px;background:#333;border-radius:4px;overflow:hidden}
.progress-fill{height:100%;width:0%;background:linear-gradient(90deg,#4facfe,#00f2fe);transition:width 0.3s}
.progress-text{text-align:center;margin-top:8px;font-size:0.85em;color:#aaa}
.status{margin-top:16px;padding:12px;border-radius:8px;text-align:center;display:none;font-size:0.9em}
.status.success{display:block;background:#1b5e20;color:#a5d6a7}
.status.error{display:block;background:#b71c1c;color:#ef9a9a}
</style>
</head>
<body>
<div class="container">
<h1>Kiro Keyboard</h1>
<p class="version">Firmware: %FW_VERSION%</p>
<div class="upload-area" id="dropArea">
<label class="file-label" for="fileInput">Select .bin file</label>
<input type="file" id="fileInput" class="file-input" accept=".bin">
<p class="file-name" id="fileName">No file selected</p>
</div>
<button class="btn" id="uploadBtn" disabled>Upload Firmware</button>
<div class="progress-wrap" id="progressWrap">
<div class="progress-bar"><div class="progress-fill" id="progressFill"></div></div>
<p class="progress-text" id="progressText">0%</p>
</div>
<div class="status" id="statusMsg"></div>
</div>
<script>
var fileInput=document.getElementById('fileInput');
var fileName=document.getElementById('fileName');
var uploadBtn=document.getElementById('uploadBtn');
var progressWrap=document.getElementById('progressWrap');
var progressFill=document.getElementById('progressFill');
var progressText=document.getElementById('progressText');
var statusMsg=document.getElementById('statusMsg');
var dropArea=document.getElementById('dropArea');

fileInput.addEventListener('change',function(){
  if(fileInput.files.length>0){
    fileName.textContent=fileInput.files[0].name+' ('+Math.round(fileInput.files[0].size/1024)+' KB)';
    uploadBtn.disabled=false;
  }
});

['dragover','dragenter'].forEach(function(e){
  dropArea.addEventListener(e,function(ev){ev.preventDefault();ev.stopPropagation();dropArea.classList.add('dragover')});
});
['dragleave','drop'].forEach(function(e){
  dropArea.addEventListener(e,function(ev){ev.preventDefault();ev.stopPropagation();dropArea.classList.remove('dragover')});
});
dropArea.addEventListener('drop',function(ev){
  if(ev.dataTransfer.files.length>0){
    fileInput.files=ev.dataTransfer.files;
    fileInput.dispatchEvent(new Event('change'));
  }
});

uploadBtn.addEventListener('click',function(){
  var file=fileInput.files[0];
  if(!file)return;
  var formData=new FormData();
  formData.append('firmware',file,file.name);
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/ota/upload',true);
  xhr.upload.addEventListener('progress',function(e){
    if(e.lengthComputable){
      var pct=Math.round(e.loaded*100/e.total);
      progressFill.style.width=pct+'%';
      progressText.textContent=pct+'%';
    }
  });
  xhr.addEventListener('load',function(){
    try{var r=JSON.parse(xhr.responseText)}catch(e){var r={ok:false,message:'Invalid response'}}
    if(r.ok){
      statusMsg.className='status success';
      statusMsg.textContent=r.message||'Update successful! Rebooting...';
    }else{
      statusMsg.className='status error';
      statusMsg.textContent=r.message||'Upload failed';
      uploadBtn.disabled=false;
    }
  });
  xhr.addEventListener('error',function(){
    statusMsg.className='status error';
    statusMsg.textContent='Connection lost. Device may be rebooting.';
  });
  uploadBtn.disabled=true;
  progressWrap.style.display='block';
  statusMsg.className='status';
  statusMsg.style.display='none';
  xhr.send(formData);
});
</script>
</body>
</html>
)rawliteral";

#endif // WIFI_OTA_PAGE_H
