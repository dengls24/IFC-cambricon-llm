import pathlib,json,hashlib,sys
root=pathlib.Path(__file__).resolve().parent.parent
items=json.loads((root/'.recovery-20261104/upload-manifest.json').read_text(encoding='utf-8'))
bad=[]
for x in items:
 p=root/x['path']
 if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=x['sha256']:bad.append(x['path'])
print('VERIFIED '+str(len(items))+' files' if not bad else 'FAILED '+str(bad))
sys.exit(bool(bad))
