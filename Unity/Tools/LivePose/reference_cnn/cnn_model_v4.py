"""CNN-v4 integer contract. Numeric/transaction reference, not an AXI cycle simulator.
Uses only NumPy; never imports torch or downloads a checkpoint.
"""
from pathlib import Path
import argparse, hashlib, json, zipfile
import numpy as np

VERSION = 'CNN-v4.0'

def rshift_even(value, n):
    a = np.asarray(value, dtype=np.int64)
    if not 0 <= n <= 31: raise ValueError('shift outside 0..31')
    if n == 0: return a.copy()
    q = a >> n
    r = a - (q << n)
    half = 1 << (n-1)
    return q + ((r > half) | ((r == half) & ((q & 1) != 0)))

def signed_bound(a, bits, label):
    a = np.asarray(a)
    if np.any(a < -(1 << (bits-1))) or np.any(a >= (1 << (bits-1))):
        raise OverflowError(label + ': signed width exceeded')

def requant(a, m, n=16, mode='body'):
    signed_bound(a,25,'requant input')
    if np.any(m < 0) or np.any(m > 131071): raise OverflowError('M must fit unsigned 17 bits')
    p = np.asarray(a,dtype=np.int64) * np.asarray(m,dtype=np.int64)
    signed_bound(p,43,'25x18 product')
    y = rshift_even(p,n)
    lo, hi = {'body':(0,127),'heatmap':(-128,127),'offset':(-32768,32767)}[mode]
    return np.clip(y,lo,hi).astype(np.int16 if mode=='offset' else np.int8)

class Weights:
    def __init__(self, archive):
        self.archive=Path(archive)
        self.z=zipfile.ZipFile(self.archive)
        self.meta=json.loads(self.z.read('weights_export/scales.json'))
        self.sha256=hashlib.sha256(self.archive.read_bytes()).hexdigest()
    def array(self,info,key,dtype):
        return np.frombuffer(self.z.read('weights_export/'+info[key]),dtype=dtype).copy()
    def layer(self,name):
        wi=self.meta[name+'.weight']; bi=self.meta[name+'.bias']
        if wi['shift_bits']!=16 or bi['shift_bits']!=16: raise ValueError('N must be 16')
        w=self.array(wi,'bin_file','i1').reshape(wi['shape'])
        m=self.array(wi,'m_bin_file','<i4').astype(np.int64)
        b=self.array(bi,'bin_file','i1').astype(np.int64)
        bm=self.array(bi,'m_bin_file','<i4').astype(np.int64)
        if len(m)!=w.shape[0] or b.size!=w.shape[0] or bm.size!=b.size: raise ValueError('channel count')
        if not np.array_equal(m,wi['per_channel_M']) or not np.array_equal(bm,bi['per_channel_M']): raise ValueError('metadata mismatch')
        ba=rshift_even(b*bm,16)
        signed_bound(ba,24,'bias_accum')
        if np.any(m<0) or np.any(m>131071): raise OverflowError('unsupported M')
        return w.astype(np.int64),m,ba

class PackedWeights:
    """Read exactly the bytes loaded by the hardware, without the export ZIP."""
    def __init__(self, path, manifest):
        self.blob=Path(path).read_bytes();self.manifest=manifest
        if len(self.blob)!=manifest['packed_bytes'] or hashlib.sha256(self.blob).hexdigest()!=manifest['packed_sha256']:
            raise ValueError('packed payload size/hash mismatch')
        self.sha256=manifest['weights_sha256'];self.layers={}
        for o in manifest['ops']:
            raw=self.blob[o['weight_offset']:o['weight_offset']+o['dma_bytes']]
            co,ci=o['cout'],o['cin'];g=(co+3)//4;t=(ci+31)//32
            q=np.frombuffer(raw[:o['param_offset']],dtype='i1')
            if o['kind']=='pw':
                w=q[:g*t*128].reshape(g,t,4,32).transpose(0,2,1,3).reshape(g*4,t*32)[:co,:ci,None,None]
            elif o['kind']=='dw':
                cb=(co+31)//32
                w=q[:cb*9*32].reshape(cb,9,32).transpose(0,2,1).reshape(cb*32,1,3,3)[:co]
            else:
                w=q[:co*32].reshape(co,32)[:,:27].reshape(co,3,3,3).transpose(0,3,1,2)
            pars=np.frombuffer(raw,dtype='<i4',count=g*4*2,offset=o['param_offset']).reshape(g*4,2)[:co]
            self.layers[o['name']]=(w.astype(np.int64),pars[:,1].astype(np.int64),pars[:,0].astype(np.int64))
    def layer(self,name):return self.layers[name]

def default_manifest(weights):
    # Geometry is the explicit implementation baseline, not a claim about unseen checkpoint code.
    ops=[]; h=256; c=3
    names=['features.conv0.conv']
    for i in range(1,14): names.extend([f'features.conv{i}.depthwise',f'features.conv{i}.pointwise'])
    names+=['heatmap','offset']
    for idx,name in enumerate(names):
        w,m,b=weights.layer(name)
        dw='depthwise' in name
        s=2 if name=='features.conv0.conv' or name in [f'features.conv{i}.depthwise' for i in (2,4,6)] else 1
        k=w.shape[-1]; d=1; p=1 if k==3 else 0
        head=name in ('heatmap','offset')
        hi=16 if head else h; ci=384 if head else c
        ho=(hi+2*p-d*(k-1)-1)//s+1
        stage=0 if idx==0 else (int(name.split('.')[1][4:]) if not head else 14+(name=='offset'))
        mode=name if head else 'body'
        ops.append(dict(op_id=idx,stage_id=int(stage),name=name,kind='dw' if dw else ('conv0' if idx==0 else 'pw'),hin=hi,win=hi,hout=ho,wout=ho,cin=ci,cout=int(w.shape[0]),kernel=k,stride=s,dilation=d,pad=p,mode=mode,shift=17 if idx==0 else 16,output_bits=16 if name=='offset' else 8))
        if not head:h=ho;c=int(w.shape[0])
    return dict(version=VERSION,weights_sha256=weights.sha256,geometry_status='IMPLEMENTATION_BASELINE_NOT_CHECKPOINT_ATTESTED',geometry_note='dilation=1/padding=1 selected from existing written specification. Run export_geometry.py against actual float model; hardware supports d=1 or 2 without interface change.',clock_hz=100000000,w_in=32,w_out=4,input_shape=[256,256,3],pixel_order='HWC_RGB',rounding='nearest_ties_even',body_scale=[6,127],heatmap_scale=[6,127],offset_scale_q16=3096,offset_bits=16,threshold_raw=-46,conv0_padding='doubled_raw_neutral_255',ops=ops)

def validate_manifest(man,weights):
    if man['version']!=VERSION or man['weights_sha256']!=weights.sha256: raise ValueError('version/hash mismatch')
    if len(man['ops'])!=29: raise ValueError('29 operations required')
    h,w,c=256,256,3
    for idx,op in enumerate(man['ops']):
        qw,m,b=weights.layer(op['name']); head=op['mode'] in ('heatmap','offset')
        if head: h,w,c=16,16,384
        if op['op_id']!=idx or (op['hin'],op['win'],op['cin'])!=(h,w,c): raise ValueError('graph shape')
        if op['dilation'] not in (1,2) or op['stride'] not in (1,2):raise ValueError('geometry unsupported')
        if op['kind']=='conv0' and (op['dilation'],op['pad'],op['stride'])!=(1,1,2):raise ValueError('conv0 requires dilation1/pad1/stride2')
        if qw.shape[0]!=op['cout'] or qw.shape[1]!=(1 if op['kind']=='dw' else c):raise ValueError('weight shape')
        if op['kernel']!=qw.shape[-1] or op['pad']!=(op['dilation'] if op['kernel']==3 else 0):raise ValueError('symmetric same padding required')
        ho=(h+2*op['pad']-op['dilation']*(op['kernel']-1)-1)//op['stride']+1
        if (ho,ho)!=(op['hout'],op['wout']):raise ValueError('output geometry')
        if op['kind']=='dw' and w*((c+31)//32)>256:raise ValueError('ring buffer exceeds selected geometry capacity')
        lim=510 if op['kind']=='conv0' else 127
        bound=np.abs(qw).reshape(qw.shape[0],-1).sum(1)*lim+np.abs(b)*(2 if op['kind']=='conv0' else 1)
        signed_bound(bound,25 if op['kind']=='conv0' else 24,'accum bound')
        if not head:h,w,c=ho,ho,op['cout']
    return True

def downsample(rgb):
    x=np.asarray(rgb)
    if x.shape!=(720,1280,3) or x.dtype!=np.uint8: raise ValueError('fixed 720x1280 RGB uint8 required')
    out=np.zeros((256,256,3),np.uint8)
    out[56:200]=x[::5,::5]
    return out

def conv_acc(x,w,b,op):
    x=np.asarray(x,dtype=np.int64)
    hi,wi,ci=x.shape; k=op['kernel'];d=op['dilation'];p=op['pad'];s=op['stride'];ho=op['hout'];wo=op['wout']
    conv0=op['kind']=='conv0'
    if conv0: x=2*x
    xp=np.pad(x,((p,p),(p,p),(0,0)),constant_values=255 if conv0 else 0)
    out=np.zeros((ho,wo,op['cout']),np.int64)
    if op['kind']=='dw':
        for ky in range(k):
            for kx in range(k):out+=xp[ky*d:ky*d+ho*s:s,kx*d:kx*d+wo*s:s,:]*w[:,0,ky,kx]
    else:
        for ky in range(k):
            for kx in range(k):out+=xp[ky*d:ky*d+ho*s:s,kx*d:kx*d+wo*s:s,:] @ w[:,:,ky,kx].T
    out+=b*(2 if conv0 else 1)
    signed_bound(out,25 if conv0 else 24,'MAC accumulator')
    return out

def forward(image,weights,manifest,dump=None):
    validate_manifest(manifest,weights)
    x=np.asarray(image)
    if x.shape!=(256,256,3) or x.dtype!=np.uint8: raise ValueError('256x256 RGB uint8 input')
    body=None; outputs={}; stats=[]
    if dump: Path(dump).mkdir(parents=True,exist_ok=True)
    for op in manifest['ops']:
        w,m,b=weights.layer(op['name'])
        inp=body if op['mode'] in ('heatmap','offset') else x
        a=conv_acc(inp,w,b,op)
        xout=requant(a,m,op['shift'],op['mode'])
        if op['mode']=='body':x=xout;body=xout
        else:outputs[op['mode']]=xout
        stats.append(dict(name=op['name'],shape=list(xout.shape),min=int(xout.min()),max=int(xout.max()),sha256=hashlib.sha256(xout.tobytes()).hexdigest()))
        if dump:np.save(Path(dump)/f"{op['op_id']:02}_{op['name']}.npy",xout)
    return outputs['heatmap'],outputs['offset'],stats

def decode(heat,offset,threshold=-46,offset_m=3096):
    if heat.shape!=(16,16,17) or offset.shape!=(16,16,34):raise ValueError('head shapes')
    result=[]
    for j in range(17):
        n=int(np.argmax(heat[:,:,j].reshape(-1))) # first row-major tie
        r,c=divmod(n,16);score=int(heat[r,c,j]);oy=int(offset[r,c,j]);ox=int(offset[r,c,j+17])
        x=int(rshift_even(c*80*65536+ox*offset_m*5,16))
        y=int(rshift_even((r*80-280)*65536+oy*offset_m*5,16))
        good=score>=threshold and 0<=x<1280 and 0<=y<720
        # Invalid coordinate result is canonical zero; raw score preserved.
        xx=x if good else 0; yy=y if good else 0
        word=((score&255)<<24)|((yy&4095)<<12)|(xx&4095)
        result.append(dict(joint=j,grid_row=r,grid_col=c,offset_y=oy,offset_x=ox,x=xx,y=yy,score_raw=score,valid=bool(good),word=word))
    return result

def color_markers(rgb,red=(160,100,100),blue=(100,100,160),min_count=8):
    a=np.asarray(rgb)[::5,:,:].astype(np.int32)
    masks=[(a[:,:,0]>=red[0])&(a[:,:,1]<=red[1])&(a[:,:,2]<=red[2]),(a[:,:,0]<=blue[0])&(a[:,:,1]<=blue[1])&(a[:,:,2]>=blue[2])]
    out=[]
    for mask in masks:
        ys,xs=np.nonzero(mask);n=len(xs);found=n>=min_count
        x=int(xs.sum())//n if found else 0;y=int((ys*5).sum())//n if found else 0
        out.append(dict(found=found,count=n,x=x,y=y,word=(int(found)<<31)|(y<<11)|x))
    return out

def pack_layer(w,m,b,kind):
    """Weights first, then 64-byte aligned parameters. All words little endian.
    PW tiles [out_group,in_batch,out_lane,in_lane], DW [batch,tap,lane].
    conv0 [out_channel,tap,RGB], padded 32 bytes per output channel.
    Params [outgroup,lane] pairs signed32 bias, unsigned32 M; dummy lanes zero.
    """
    co,ci,kh,kw=w.shape
    if kind=='pw':
        wp=np.zeros(((co+3)//4, (ci+31)//32,4,32),np.int8)
        for o in range(co):
            for i in range(ci):wp[o//4,i//32,o%4,i%32]=w[o,i,0,0]
    elif kind=='dw':
        wp=np.zeros(((co+31)//32,9,32),np.int8)
        for o in range(co):wp[o//32,:,o%32]=w[o,0].reshape(-1)
    else:
        wp=np.zeros((co,32),np.int8)
        wp[:,:27]=w.transpose(0,2,3,1).reshape(co,27)
    raw=bytearray(wp.tobytes()); param_offset=(len(raw)+63)//64*64
    raw.extend(bytes(param_offset-len(raw)))
    count=((co+3)//4)*4
    params=np.zeros((count,2),dtype='<i4');params[:co,0]=b;params[:co,1]=m
    raw.extend(params.tobytes());raw.extend(bytes((-len(raw))%64))
    return bytes(raw),param_offset

def make_package(archive,out,geometry=None):
    out=Path(out);out.mkdir(parents=True,exist_ok=True);weights=Weights(archive);man=default_manifest(weights)
    if geometry:
        cfg=json.loads(Path(geometry).read_text())
        for op in man['ops']:
            if op['name'] in cfg['layers']:
                g=cfg['layers'][op['name']];op.update({k:g[k] for k in ['stride','dilation','pad']})
        man['geometry_status']='CHECKPOINT_MODULES_EXPORTED';man['geometry_source']=cfg.get('source',{})
    validate_manifest(man,weights)
    blob=bytearray()
    for op in man['ops']:
        w,m,b=weights.layer(op['name']);raw,po=pack_layer(w,m,b,op['kind'])
        op['weight_offset']=len(blob);op['param_offset']=po;op['dma_bytes']=len(raw);blob.extend(raw)
    man['packed_sha256']=hashlib.sha256(blob).hexdigest();man['packed_bytes']=len(blob)
    (out/'weights_v4.bin').write_bytes(blob);(out/'manifest.json').write_text(json.dumps(man,indent=2,ensure_ascii=False))
    return man

def main():
    p=argparse.ArgumentParser();sub=p.add_subparsers(dest='cmd',required=True)
    b=sub.add_parser('pack');b.add_argument('--weights',required=True);b.add_argument('--out',required=True);b.add_argument('--geometry')
    r=sub.add_parser('run');src=r.add_mutually_exclusive_group(required=True);src.add_argument('--weights');src.add_argument('--packed');r.add_argument('--manifest',required=True);r.add_argument('--image',required=True,help='RGB uint8 .npy 720x1280x3 or 256x256x3');r.add_argument('--out',required=True);r.add_argument('--dump',action='store_true')
    a=p.parse_args()
    if a.cmd=='pack':
        m=make_package(a.weights,a.out,a.geometry);print(json.dumps({'version':VERSION,'packed_bytes':m['packed_bytes'],'geometry_status':m['geometry_status']}));return
    out=Path(a.out);out.mkdir(parents=True,exist_ok=True);im=np.load(a.image,allow_pickle=False);markers=None
    if im.shape==(720,1280,3):markers=color_markers(im);im=downsample(im)
    m=json.loads(Path(a.manifest).read_text());hm,off,stats=forward(im,PackedWeights(a.packed,m) if a.packed else Weights(a.weights),m,out/'features' if a.dump else None)
    result=decode(hm,off,m['threshold_raw'],m['offset_scale_q16'])
    np.save(out/'heatmap.npy',hm);np.save(out/'offset.npy',off)
    (out/'results.json').write_text(json.dumps({'version':VERSION,'geometry_status':m['geometry_status'],'joints':result,'markers':markers,'layers':stats},indent=2))
    print('saved',out)

if __name__=='__main__':main()
