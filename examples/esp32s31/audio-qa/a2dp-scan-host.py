import subprocess,time,json,re
from pathlib import Path
import os
addr=os.environ['S31_QA_BT_ADDRESS']
sink='bluez_output.'+addr.replace(':','_')+'.1'
base='/org/bluez/hci0/dev_'+addr.replace(':','_')
def run(*args):return subprocess.run(args,capture_output=True,text=True)
def get():return json.loads(subprocess.check_output(['pw-dump'],text=True))
def node(a,name):return next((n for n in a if n.get('info',{}).get('props',{}).get('node.name')==name),None)
def ports(a,nid,d):return {x['info']['props'].get('audio.channel'):x['id'] for x in a if x['type'].endswith(':Port') and str(x['info']['props'].get('node.id'))==str(nid) and x['info']['direction']==d}
pair=connect=play=None;nid=None
try:
 for attempt in range(3):
  a=run('bluetoothctl','--timeout','10','scan','bredr')
  print('DISCOVERY_ATTEMPT',attempt+1,'RC',a.returncode,flush=True)
  if addr in run('bluetoothctl','devices').stdout:break
 else:raise RuntimeError('authorized receiver not discovered')
 with open('live-pair.log','w') as log:
  pair=subprocess.Popen(['bluetoothctl','--timeout','60','pair',addr],stdout=log,stderr=subprocess.STDOUT)
  for _ in range(60):
   info=run('bluetoothctl','info',addr).stdout
   if 'Paired: yes' in info and 'Connected: yes' in info and 'Audio Sink' in info:break
   time.sleep(.5)
  else:raise RuntimeError('normal pairing not confirmed')
  print('PAIR_AND_SDP_PASS',flush=True)
  connect=subprocess.Popen(['bluetoothctl','--timeout','25','connect',addr],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  for _ in range(50):
   a=get();n=node(a,sink)
   if n:break
   time.sleep(.3)
  else:raise RuntimeError('test sink not available')
  nid=n['id'];print('A2DP_NODE_READY',nid,flush=True)
  subprocess.run(['wpctl','set-mute',str(nid),'0'],check=True)
  subprocess.run(['wpctl','set-volume',str(nid),'0.40'],check=True)
  for attempt in range(8):
   time.sleep(.5)
   v=run('wpctl','get-volume',str(nid)).stdout.strip()
   m=re.search(r'Volume: ([0-9.]+)',v)
   if m and float(m.group(1))>0.1 and '[MUTED]' not in v:break
   subprocess.run(['wpctl','set-volume',str(nid),'0.40'],check=True)
  else:raise RuntimeError('nonzero sink volume not confirmed')
  print('SINK_VOLUME',v,flush=True)
  play=subprocess.Popen(['pw-play','--target','0','--properties','{"node.name":"s31-live-tone","node.autoconnect":false}','tone-stereo.wav'])
  for _ in range(40):
   a=get();tone=node(a,'s31-live-tone')
   if tone:
    outs=ports(a,tone['id'],'output');ins=ports(a,nid,'input')
    if all(ch in outs and ch in ins for ch in ('FL','FR')):break
   time.sleep(.15)
  else:raise RuntimeError('audio ports missing')
  subprocess.run(['wpctl','set-mute',str(tone['id']),'0'],check=True)
  subprocess.run(['wpctl','set-volume',str(tone['id']),'1.0'],check=True)
  print('STREAM_VOLUME',run('wpctl','get-volume',str(tone['id'])).stdout.strip(),flush=True)
  for ch in ('FL','FR'):subprocess.run(['pw-link',str(outs[ch]),str(ins[ch])],check=True)
  print('EXPLICIT_STEREO_LINK_PASS',flush=True)
  seen=False
  while play.poll() is None:
   tree=run('busctl','--system','--list','tree','org.bluez').stdout
   for path in (v.strip() for v in tree.splitlines() if base+'/' in v and re.search(r'/fd\d+$',v.strip())):
    a=run('busctl','--system','get-property','org.bluez',path,'org.bluez.MediaTransport1','State')
    if '"active"' in a.stdout and not seen:
     Path('live-active.marker').write_text(str(time.monotonic()));print('A2DP_ACTIVE_PASS',flush=True);seen=True
   time.sleep(.2)
  assert play.returncode==0 and seen,'media transport not active'
  Path('live-end.marker').write_text(str(time.monotonic()));print('LIVE_PLAY_PASS',flush=True)
  time.sleep(1)
finally:
 if play and play.poll() is None:play.terminate();play.wait(timeout=5)

 if connect and connect.poll() is None:connect.terminate();connect.wait(timeout=5)
 run('bluetoothctl','--timeout','8','disconnect',addr)
 if pair and pair.poll() is None:pair.terminate();pair.wait(timeout=5)
