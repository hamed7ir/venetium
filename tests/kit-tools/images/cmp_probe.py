import json, sys, imgtools as T
S='F:/cr/src/third_party/'
def read_ppm(p):
    d=open(p,'rb').read(); parts=d.split(None,4); w,h=int(parts[1]),int(parts[2]); off=len(d)-w*h*3; body=d[off:]
    return [[(body[(y*w+x)*3],body[(y*w+x)*3+1],body[(y*w+x)*3+2],255) for x in range(w)] for y in range(h)]
def grid_white(pix,gx=8,gy=8):
    h=len(pix); w=len(pix[0]); out=[]
    for j in range(gy):
        y0,y1=j*h//gy,(j+1)*h//gy
        for i in range(gx):
            x0,x1=i*w//gx,(i+1)*w//gx
            s=[0,0,0]; n=0
            for y in range(y0,y1):
                for x in range(x0,x1):
                    r,g,b,a=pix[y][x]
                    for k,v in enumerate((r,g,b)): s[k]+=(v*a+255*(255-a))/255
                    n+=1
            out.append([round(v/n) for v in s])
    return out
refs={}
cw=T.read_png(open(S+'skia/resources/images/color_wheel.png','rb').read())[2]
bt=T.read_png(open(S+'skia/resources/images/baby_tux.png','rb').read())[2]
refs['color_wheel']=grid_white(cw); refs['baby_tux']=grid_white(bt)
refs['test.webp']=grid_white(read_ppm(S+'libwebp/src/examples/test_ref.ppm'))
refs['testorig']=grid_white(read_ppm(S+'libjpeg_turbo/testimages/testorig.ppm'))
res=json.load(open(sys.argv[1]))
for name,r in res.items():
    if 'grid' not in r: print(name,r.get('ev'),'(no grid)'); continue
    key = 'color_wheel' if name.startswith('color_wheel') else 'baby_tux' if name.startswith('baby_tux') else 'test.webp' if name=='test.webp' else 'testorig' if name.startswith('test') and name.endswith('.jpg') else None
    if not key: print(name,r['w'],r['h'],r['sum'],'(no ref)'); continue
    ref=refs[key]; mx=0; tot=0
    for a,b in zip(r['grid'],ref):
        for x,y in zip(a,b): mx=max(mx,abs(x-y)); tot+=abs(x-y)
    print('%-34s %4dx%-4d sum %-9s max cell diff %3d  mean %.2f'%(name,r['w'],r['h'],r['sum'],mx,tot/(64*3)))
