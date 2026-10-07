import struct
def segments(d):
    """list of (marker, offset, length_including_marker_bytes, payload_offset) up to and including SOS; returns also scan data start/end"""
    assert d[:2]==b'\xff\xd8'
    p=2; segs=[]
    while p<len(d):
        assert d[p]==0xff, (p, d[p])
        m=d[p+1]
        if m==0xd8 or (0xd0<=m<=0xd7) or m==0x01: segs.append((m,p,2)); p+=2; continue
        if m==0xd9: segs.append((m,p,2)); p+=2; break
        n=struct.unpack('>H',d[p+2:p+4])[0]
        segs.append((m,p,2+n))
        p+=2+n
        if m==0xda:
            # skip entropy-coded data to the next marker that is not RSTn / stuffed
            while True:
                q=d.find(b'\xff',p)
                if q<0 or q+1>=len(d): p=len(d); break
                nm=d[q+1]
                if nm==0 or 0xd0<=nm<=0xd7: p=q+2; continue
                segs.append(('scan',segs[-1][1]+segs[-1][2],q-(segs[-1][1]+segs[-1][2]))); p=q; break
    return segs
if __name__=='__main__':
    import sys
    for f in sys.argv[1:]:
        d=open(f,'rb').read()
        print(f,len(d))
        for s in segments(d): print('  ',s if s[0]=='scan' else (hex(s[0]),)+s[1:])
