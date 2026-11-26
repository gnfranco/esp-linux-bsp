from pathlib import Path
import subprocess, os, concurrent.futures
root=Path('.').resolve()
cc=os.environ.get('S31_CC','riscv32-esp-linux-musl-gcc')
incs=['port/linux','src','3rd-party/bluedroid/decoder/include','3rd-party/bluedroid/encoder/include','3rd-party/lc3-google/include','3rd-party/md5','3rd-party/yxml','platform/embedded','platform/posix','platform/linux']
flags=['-Os','-mabi=ilp32','-DS31_A2DP_SBC_MAX_BITPOOL=53','-DS31_A2DP_SBC_CAPABILITIES_0=0x21','-DS31_BTSTACK_TRANSPORT_ONLY=0','-DS31_BTSTACK_FORCE_CLASSIC=1','-DHAVE_POSIX_FILE_IO','-ffunction-sections','-fdata-sections']+['-I'+i for i in incs]
files=[p for g in ['src/*.c','src/ble/*.c','src/classic/*.c','3rd-party/bluedroid/decoder/srce/*.c'] for p in root.glob(g)]
files += [root/p for p in ['platform/posix/btstack_run_loop_posix.c','platform/posix/btstack_signal.c','platform/posix/btstack_stdin_posix.c','platform/posix/btstack_tlv_posix.c','platform/posix/hci_dump_posix_fs.c','platform/posix/wav_util.c','platform/linux/hci_transport_linux.c','port/linux/main.c','example/a2dp_sink_demo.c']]
out=root/'qa-objects';out.mkdir(exist_ok=True)
def build(p):
 o=out/(str(p.relative_to(root)).replace('/','_')+'.o')
 if not o.exists() or o.stat().st_mtime<p.stat().st_mtime:
  proc=subprocess.run([cc,*flags,'-c',str(p),'-o',str(o)],capture_output=True,text=True)
  if proc.returncode: raise RuntimeError(str(p)+'\n'+proc.stderr)
 return str(o)
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool: objects=list(pool.map(build,files))
subprocess.run([cc,'-mabi=ilp32','-Wl,--gc-sections','-o','s31-btstack-a2dp-qa',*objects,'-lpthread','-lm'],check=True)
subprocess.run([cc.removesuffix('gcc')+'strip','s31-btstack-a2dp-qa'],check=True)
print('QA_BUILD_PASS',flush=True)
