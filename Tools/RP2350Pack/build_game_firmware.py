#!/usr/bin/env python3
"""Build the full RP2350 engine and independently flashable native resources."""
import argparse,json,subprocess,sys
from pathlib import Path
from verify_split_uf2 import verify
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('pak',type=Path);p.add_argument('-o','--output',type=Path,default=ROOT/'build-host/rp2350-release');p.add_argument('--pico8c-root',type=Path,default=ROOT.parent/'pico8c');p.add_argument('--picotool-dir',type=Path);p.add_argument('--engine-test-python',type=Path,help='Python with unicorn and pyelftools; require gameplay regression tests before reporting success');p.add_argument('--reuse-resources',action='store_true');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);(out/'release-verification.json').unlink(missing_ok=True);pico=a.pico8c_root.resolve();toolchains=sorted((pico/'third_party/toolchains').glob('arm-gnu-toolchain-*'))
if not toolchains:raise SystemExit('ARM GCC not found in pico8c')
pt=a.picotool_dir or pico/'build/template-rp2350/_deps/picotool'
def run(cmd):subprocess.run(list(map(str,cmd)),check=True)
if not a.reuse_resources:run([sys.executable,ROOT/'Tools/RP2350Pack/run_pipeline.py',a.pak,'-o',out/'resources','--resource-profile','rp2350-game','--pico8c-root',pico])
run(['cmake','-S',ROOT/'platform/rp2350','-B',out/'firmware','-G','Ninja','-DPICO_SDK_PATH='+str(pico/'third_party/pico-sdk'),'-DPICO_TOOLCHAIN_PATH='+str(toolchains[-1]),'-Dpicotool_DIR='+str(pt),'-DCMAKE_BUILD_TYPE=MinSizeRel'])
run(['cmake','--build',out/'firmware','--target','quake_rp2350','-j6'])
f=out/'firmware/game';r=out/'resources'
report=verify((f/'quake_rp2350.uf2').read_bytes(),(r/'quake-resources.uf2').read_bytes(),(f/'quake_rp2350.bin').read_bytes(),(r/'quake-native.qrn').read_bytes())
engine_tests={}
if a.engine_test_python:
    cases={
        'skill_portal':['--teleport-test','easy','--frames','60'],
        'episode_portal':['--episode-test','--frames','100'],
        'changelevel_cycle':['--changelevel-cycle','--cycle','50','--frames','450'],
        'input_matrix':['--input-matrix','--frames','130'],
    }
    for name,options in cases.items():
        result=out/(name+'.json')
        run([a.engine_test_python,ROOT/'Tools/RP2350Pack/test_arm_firmware.py',f/'quake_rp2350.elf',r/'quake-native.qrn',*options,'--json',result])
        engine_tests[name]=json.loads(result.read_text())
        if not engine_tests[name]['passed']:raise SystemExit('Engine regression failed: '+name)
report.update(engine_tested=bool(engine_tests),engine_tests=engine_tests)
report.update(target='full MG24 Quake engine / RP2350',hardware_tested=False,persistent_saves=False)
(out/'release-verification.json').write_text(json.dumps(report,indent=2)+'\n')
print('Program:',f/'quake_rp2350.uf2');print('Resources:',r/'quake-resources.uf2')
