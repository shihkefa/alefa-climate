from pathlib import Path
import zipfile, hashlib, json
root=Path(__file__).resolve().parent.parent
paths=[root/'Universal-C6-Pairing.yaml',root/'pairing-qa/universal_build.py']
paths+=sorted(p for p in (root/'universal_c6_components').rglob('*') if p.is_file() and '__pycache__' not in p.parts)
manifest={}
with zipfile.ZipFile(root/'pairing-qa/universal-upload.zip','w',zipfile.ZIP_DEFLATED) as z:
    for p in paths:
        name=p.relative_to(root).as_posix(); z.write(p,name); manifest[name]=hashlib.sha256(p.read_bytes()).hexdigest()
(root/'pairing-qa/universal-source-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
print(f'Prepared {len(paths)} source files; no credentials included')
