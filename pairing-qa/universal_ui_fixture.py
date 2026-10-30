"""Set public, loopback-only preview data. Never accesses a physical device."""
import json,sys,urllib.request
phase=sys.argv[1]
assert phase in ('ac','offline','dehumidifier','migration')
appliance=dict(status='connected',type='air_conditioner',protocol='TaiSEIA',brand='HITACHI',
               model='UI demo',online=True,locked=True,return_air_temperature=26.0)
state=dict(schema_version=3,paired=False,open=True,ready=True,setup=False,wifi=False,status_unavailable=False)
if phase=='offline':appliance.update(status='detecting',online=False,return_air_temperature=None)
if phase=='dehumidifier':appliance.update(type='dehumidifier',return_air_temperature=None)
if phase=='migration':
    appliance.update(status='reset_required',type='unknown',online=False,locked=False,return_air_temperature=None)
    state.update(ready=False,open=False)
state['appliance']=appliance
req=urllib.request.Request('http://127.0.0.1:8768/test/state',data=json.dumps(state).encode(),
                           headers={'Content-Type':'application/json'},method='POST')
with urllib.request.urlopen(req,timeout=5) as response:assert json.load(response)['ok']
print('Public preview fixture:',phase)
