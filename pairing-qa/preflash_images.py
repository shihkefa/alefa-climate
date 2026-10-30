"""Read-only verification of ESP32-C6 images, partitions, release and source hashes.
Format reference: Espressif esptool firmware-image-format documentation.
Never opens a serial port or flashes hardware.
"""
from pathlib import Path
import argparse, hashlib, json, struct, zipfile
p=argparse.ArgumentParser();p.add_argument('release',type=Path);a=p.parse_args()
release=a.release
sha=lambda data:hashlib.sha256(data).hexdigest()
meta=json.loads((release/'build-info.json').read_text(encoding='utf8'))
for line in (release/'SHA256SUMS.txt').read_text().splitlines():
    expected,name=line.split('  ',1);assert sha((release/name).read_bytes())==expected,name
for name,info in meta['files'].items():
    data=(release/name).read_bytes();assert len(data)==info['bytes'] and sha(data)==info['sha256']
def image(data,label):
    magic,count,mode,size_freq,entry=struct.unpack_from('<BBBBI',data)
    chip=struct.unpack_from('<H',data,12)[0]
    assert magic==0xe9 and 0<count<=16 and chip==13,(label,magic,count,chip)
    assert mode==2 and size_freq>>4==2,(label,mode,size_freq)
    offset=24;check=0xef;segments=[]
    for _ in range(count):
        address,length=struct.unpack_from('<II',data,offset);offset+=8
        assert offset+length<=len(data)
        payload=data[offset:offset+length]
        for byte in payload:check^=byte
        segments.append((address,payload));offset+=length
    checksum_offset=(offset//16+1)*16-1
    assert data[checksum_offset]==check,(label,'segment XOR checksum')
    image_end=checksum_offset+1
    assert data[23]==1,(label,'missing image digest')
    assert hashlib.sha256(data[:image_end]).digest()==data[image_end:image_end+32],(label,'image digest')
    print('PASS:',label,'ESP32-C6, DIO, 4 MB;',count,'segments; XOR and appended SHA256 valid')
    return image_end+32,segments
factory=(release/'alefa-universal.factory.bin').read_bytes()
app=(release/'alefa-universal.app.bin').read_bytes()
boot_end,_=image(factory,'bootloader @0x0');assert boot_end<=0x8000
app_end,segments=image(app,'application @0x10000');assert app_end==len(app)
assert factory[0x10000:0x10000+len(app)]==app
table=factory[0x8000:0x9000];entries=[]
for pos in range(0,len(table),32):
    magic=struct.unpack_from('<H',table,pos)[0]
    if magic==0xebeb:
        assert table[pos+16:pos+32]==hashlib.md5(table[:pos]).digest()
        break
    assert magic==0x50aa
    _,kind,sub,offset,size,label,flags=struct.unpack_from('<HBBII16sI',table,pos)
    entries.append(dict(name=label.split(b'\0')[0].decode(),type=kind,subtype=sub,offset=offset,size=size,flags=flags))
else:raise AssertionError('Partition table missing MD5')
assert [(x['name'],x['offset'],x['size']) for x in entries]==[
    ('phy_init',0x9000,0x1000),('factory',0x10000,0x380000),('nvs',0x390000,0x70000)]
assert all(x['flags']==0 for x in entries)
assert len(app)<=0x380000 and len(factory)<=0x390000
desc=segments[0][1];assert struct.unpack_from('<I',desc)[0]==0xabcd5432
text=lambda off,n:desc[off:off+n].split(b'\0')[0].decode()
print('APP descriptor:',json.dumps(dict(version=text(16,32),project=text(48,32),time=text(80,16),date=text(96,16),idf=text(112,32))))
print('PASS: partition MD5, no OTA partition, app placement and 4 MB bounds; NVS excluded from factory image')
for path in sorted(release.glob('*.zip'))+sorted((release/'source').glob('*.zip')):
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        for line in z.read('SHA256SUMS.txt').decode().splitlines():
            expected,name=line.split('  ',1);assert sha(z.read(name))==expected,(path.name,name)
    print('PASS: archive CRC and SHA256 manifest:',path.name)
for line in (release/'RELEASE-SHA256.txt').read_text().splitlines():
    expected,name=line.split('  ',1);assert sha((release/name).read_bytes())==expected
print('PASS: all release hashes match; no serial hardware accessed')
