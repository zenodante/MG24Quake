#!/usr/bin/env python3
"""Exercise the full server/client/renderer with stock Episode 1 maps."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--player',type=Path,required=True)
p.add_argument('--assets',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
report=a.output/'game-report.json';log=a.output/'game.log'
command=[str(a.player.resolve()),'--assets',str(a.assets.resolve()),'--frames','3240','--cycle','360','--scripted','--headless','--uncapped','--report',str(report.resolve()),'--capture',str((a.output/'game.bmp').resolve())]
with log.open('w') as f:subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
r=json.loads(report.read_text())
assert not r['failed'] and r['full_engine_host_frame']
assert r['frames']==3240 and r['server_active'] and r['signon']==4
assert len(r['signed_on_frames_by_map'])==9
assert all(n>=300 for n in r['signed_on_frames_by_map'].values()),r['signed_on_frames_by_map']
assert r['shells']<25 and r['sound_starts']>0 and r['adpcm_samples']>0
assert r['zone_allocation_peak_bytes']<=r['zone_capacity_bytes']
assert r['framebuffer_bytes']==320*200 and r['line_buffers_bytes']==2*320*2
print('PASS: 9 maps, 3240 Host_Frame calls, player firing, ADPCM and memory report')
print(report)
