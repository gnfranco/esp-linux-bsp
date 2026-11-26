import serial,time,re,base64,gzip,hashlib,sys,os
from pathlib import Path
s=serial.Serial(port=None,baudrate=115200,timeout=.15,exclusive=True);s.port=os.environ.get('S31_QA_UART','/dev/ttyUSB0');s.dtr=False;s.rts=False;s.open()
def recv(marker,limit):
 end=time.monotonic()+limit;b=b''
 while time.monotonic()<end:
  b+=s.read(s.in_waiting or 1)
  if marker in b:return b
 raise RuntimeError('serial response timeout')
def cmd(c,limit=15):
 s.write((c+"; printf '\\n__AUDIO%s__\\n' DONE\r").encode())
 return recv(b'__AUDIODONE__',limit)
try:
 end=time.monotonic()+50;buf=b'';last=0
 while time.monotonic()<end:
  buf+=s.read(s.in_waiting or 1)
  if re.search(rb'login:\s*$',buf):s.write(b'root\r');buf+=b'\n'
  if re.search(rb'(?m)^~ #\s*$',buf):break
  if time.monotonic()-last>4:s.write(b'\r');last=time.monotonic()
 else:raise RuntimeError('console unavailable')
 print('TARGET_SHELL_READY',flush=True)
 a=cmd('grep -q " /run tmpfs " /proc/mounts || mount -t tmpfs -o size=1m tmpfs /run; grep -q " /tmp tmpfs " /proc/mounts || mount -t tmpfs -o size=2m tmpfs /tmp; mkdir -p /run/lock; cat /sys/module/esp32s31_radio/parameters/mode; ls -l /dev/s31-hci')
 if not re.search(rb'(?m)^combo\r?$',a):
  a=cmd('export ESP32_CONFIG_DIR=/run/esp32-conf; S31_LEGACY_TKIP=0 S31_RADIO_VOLATILE_MODE=combo /etc/init.d/S00s31-radio start >/tmp/radio-start.log 2>&1; cat /sys/module/esp32s31_radio/parameters/mode; ls -l /dev/s31-hci',50)
 assert re.search(rb'(?m)^combo\r?$',a) and b'/dev/s31-hci' in a
 print('COMBO_RADIO_READY',flush=True)
 data=Path('community-src/cache/sources/btstack/s31-btstack-a2dp-qa').read_bytes()
 payload=base64.b64encode(gzip.compress(data)).decode()
 cmd('stty -echo; umask 077; : > /tmp/qa-a2dp.b64')
 for i in range(0,len(payload),1800):
  cmd("printf '%s' '"+payload[i:i+1800]+"' >> /tmp/qa-a2dp.b64")
 a=cmd('base64 -d /tmp/qa-a2dp.b64 | gzip -d > /tmp/qa-a2dp; chmod 700 /tmp/qa-a2dp; sha256sum /tmp/qa-a2dp; rm -f /tmp/qa-a2dp.b64; stty echo')
 assert hashlib.sha256(data).hexdigest().encode() in a
 print('A2DP_BINARY_HASH_PASS',flush=True)
 a=cmd('grep -q " /var/lib/btstack tmpfs " /proc/mounts || mount -t tmpfs -o size=128k tmpfs /var/lib/btstack; S31_HCI_DEVICE=/dev/s31-hci S31_BTSTACK_COEX=1 S31_BTSTACK_NAME=S31-CLASSIC-QA-2511 /tmp/qa-a2dp -u 0 -l none >/tmp/qa-a2dp.log 2>&1 & echo $! >/tmp/qa-a2dp.pid; sleep 3; tail -25 /tmp/qa-a2dp.log',20)
 print(a.decode(errors='replace'),flush=True)
 assert b'BTstack up and running' in a
 print('A2DP_RECEIVER_READY',flush=True)
 # Prepare WLAN for a passive scan, without associating to any network.
 a=cmd('ip link set wlan0 up; echo WLAN_UP_RC=$?')
 assert b'WLAN_UP_RC=0' in a
 print('WLAN_SCAN_READY',flush=True)
 for name in ('live-active.marker','live-end.marker'):Path(name).unlink(missing_ok=True)
 scan_started=False
 # Keep the UART open throughout the actual Bluetooth session.
 deadline=time.monotonic()+150
 while time.monotonic()<deadline:
  if Path('live-active.marker').exists() and not scan_started:
   stamp=time.monotonic()
   a=cmd('(iw dev wlan0 scan passive >/tmp/qa-wlan-scan.log 2>&1; echo $? >/tmp/qa-wlan-scan.rc) & echo SCAN_LAUNCHED')
   scan_started=True
   Path('scan-start.marker').write_text(str(stamp))
   print('WLAN_SCAN_REQUESTED_DURING_A2DP',flush=True)
  a=cmd("tail -35 /tmp/qa-a2dp.log; if [ -f /tmp/s31-qa-pcm.wav ]; then wc -c /tmp/s31-qa-pcm.wav; fi")
  if re.search(rb'(?m)^QA_PCM packets=\d+',a):
   print(a.decode(errors='replace'),flush=True)
   break
  time.sleep(.4)
 else:
  print(a.decode(errors='replace'),flush=True)
  raise RuntimeError('audio capture not completed')
 assert scan_started,'scan not requested'
 a=cmd('for i in $(seq 1 20); do test -f /tmp/qa-wlan-scan.rc && break; sleep 1; done; printf "SCAN_RC="; cat /tmp/qa-wlan-scan.rc; grep -c "^BSS " /tmp/qa-wlan-scan.log || true',30)
 print(a.decode(errors='replace'),flush=True)
 a=cmd('sha256sum /tmp/s31-qa-pcm.wav')
 m=re.search(rb'(?m)^([0-9a-f]{64})  /tmp/s31-qa-pcm.wav',a)
 assert m,'target WAV hash missing'
 cmd('stty -echo')
 a=cmd("printf '\\nPCM64_BEGIN\\n'; gzip -c /tmp/s31-qa-pcm.wav | base64; printf '\\nPCM64_END\\n'",160)
 data=gzip.decompress(base64.b64decode(a.split(b'\nPCM64_BEGIN\r\n',1)[1].split(b'\nPCM64_END',1)[0]))
 assert hashlib.sha256(data).hexdigest().encode()==m.group(1)
 Path('received-a2dp-wavfix-scan.wav').write_bytes(data)
 print('LIVE_WAV_HASH_PASS bytes='+str(len(data)),flush=True)
finally:
 try:
  a=cmd('stty echo; if [ -f /tmp/qa-a2dp.pid ]; then kill -INT $(cat /tmp/qa-a2dp.pid) 2>/dev/null; fi; echo QA_TARGET_STOPPED')
  print('TARGET_STOP_CONFIRMED' if b'QA_TARGET_STOPPED' in a else 'TARGET_STOP_UNCONFIRMED',flush=True)
 except Exception:print('TARGET_STOP_UNCONFIRMED',flush=True)
 s.close()
