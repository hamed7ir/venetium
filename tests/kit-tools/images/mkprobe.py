import sys, base64, json, os, mimetypes
# usage: mkprobe.py out.html file1 file2 ...   (name = basename; type by extension)
out = sys.argv[1]; files = sys.argv[2:]
TYPES = {'jpg':'image/jpeg','jpeg':'image/jpeg','png':'image/png','gif':'image/gif','webp':'image/webp','avif':'image/avif','bmp':'image/bmp','ico':'image/x-icon'}
items = []
for f in files:
    d = open(f,'rb').read()
    ext = f.rsplit('.',1)[-1].lower()
    items.append({'name': os.path.basename(f), 'type': TYPES.get(ext,'application/octet-stream'), 'b64': base64.b64encode(d).decode(), 'n': len(d)})
html = '''<!doctype html><meta charset="utf-8"><title>probe</title>
<body style="font:12px monospace;background:#ddd"><div id=o></div>
<script>
const ITEMS = %s;
function fnv(a){let h=0x811c9dc5;for(let i=0;i<a.length;i++){h^=a[i];h=Math.imul(h,0x01000193)>>>0;}return h;}
function bytes(b64){const bin=atob(b64);const u=new Uint8Array(bin.length);for(let i=0;i<bin.length;i++)u[i]=bin.charCodeAt(i);return u;}
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
  const res={};
  for(const it of ITEMS){
    const r={n:it.n}; res[it.name]=r;
    const url=URL.createObjectURL(new Blob([bytes(it.b64)],{type:it.type}));
    const img=new Image();
    const ev=await new Promise(ok=>{img.onload=()=>ok('load');img.onerror=()=>ok('error');setTimeout(()=>ok('timeout'),8000);img.src=url;});
    r.ev=ev; r.w=img.naturalWidth; r.h=img.naturalHeight;
    if(ev==='load'){
      try{await img.decode();r.decode='ok';}catch(e){r.decode=String(e.name+': '+e.message);}
      try{
        const c=document.createElement('canvas');c.width=img.naturalWidth;c.height=img.naturalHeight;
        const x=c.getContext('2d');x.drawImage(img,0,0);
        const d=x.getImageData(0,0,c.width,c.height).data;
        r.sum=fnv(d).toString(16);
        {const W=c.width,H=c.height,GX=8,GY=8,g=[];for(let j=0;j<GY;j++){const y0=Math.floor(j*H/GY),y1=Math.floor((j+1)*H/GY);for(let i=0;i<GX;i++){const x0=Math.floor(i*W/GX),x1=Math.floor((i+1)*W/GX);let s=[0,0,0],n=0;for(let y=y0;y<y1;y++)for(let x=x0;x<x1;x++){const o=(y*W+x)*4,a=d[o+3];for(let k=0;k<3;k++)s[k]+=(d[o+k]*a+255*(255-a))/255;n++;}g.push(s.map(v=>Math.round(v/n)));}}r.grid=g;}r.px0=Array.from(d.slice(0,8));
        let nz=0;for(let i=3;i<d.length;i+=4)if(d[i])nz++;r.opaque=nz;
        const lab=document.createElement('div');lab.textContent=it.name+' '+c.width+'x'+c.height+' '+r.sum;
        document.getElementById('o').append(lab,c);
        c.style.cssText='border:1px solid #000;background:#fff;max-width:200px;margin:2px';
      }catch(e){r.draw=String(e);}
    }
  }
  window.__probe=res;
})();
</script>''' % json.dumps(items)
open(out,'w').write(html)
