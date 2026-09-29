#!/usr/bin/env python3
"""Link the full portable MG24 engine with the RP2350 SDK for Flash SIZE ONLY.

Produces no UF2. Native target resource binding is a fail-fast placeholder and
RAM placement is synthetic to accommodate legacy host Flash simulation arrays.
Do not flash these ELFs/binaries or infer SRAM fit/bootability from this audit.
"""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
EXCLUDE={'vid_sdl','snd_sdl','debug','snd_dma','snd_mem','snd_mix','d_surf','r_sky'}

def run(cmd,cwd,log):
    with log.open('w') as f:
        r=subprocess.run(list(map(str,cmd)),cwd=cwd,stdout=f,stderr=subprocess.STDOUT)
    if r.returncode:raise RuntimeError(f'Command failed: see {log}')

def sdk_commands(out,pico,gcc,nano):
    sdk=out/('sdk-nano' if nano else 'sdk');sdk.mkdir(parents=True,exist_ok=True)
    cmake=shutil.which('cmake');ninja=shutil.which('ninja')
    if not cmake or not ninja:raise ValueError('cmake and ninja are required')
    flags='-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mcmse -mfloat-abi=softfp -ftls-model=local-exec'
    if nano:flags+=' --specs=nano.specs'
    run([cmake,'-S',ROOT/'platform/rp2350','-B',sdk,'-G','Ninja',
         '-DPICO_SDK_PATH='+str(pico/'third_party/pico-sdk'),
         '-DPICO_TOOLCHAIN_PATH='+str(gcc.parent.parent),'-DCMAKE_BUILD_TYPE=MinSizeRel',
         '-DPICO_NO_PICOTOOL=1','-DCMAKE_C_FLAGS='+flags,'-DCMAKE_CXX_FLAGS='+flags],ROOT,sdk/'configure.log')
    commands=subprocess.check_output([ninja,'-C',str(sdk),'-t','commands','quake_rp2350_bringup'],text=True).splitlines()
    tokens=shlex.split(commands[-1]);link=tokens[2:tokens.index('&&',2)]
    # Build object dependencies, not the diagnostic's post-build UF2 converter.
    # Audits intentionally do not install/download picotool or emit flash images.
    run([ninja,'-C',sdk,'-j6',*[x for x in link if x.endswith('.o')]],ROOT,sdk/'objects.log')
    compile=next(shlex.split(x) for x in commands if '-c '+str(ROOT/'platform/rp2350/main.c') in x)
    return sdk,compile,link

def build(profile,out,gcc,sdk,platform_compile,base_link):
    target=out/profile;target.mkdir(exist_ok=True)
    lto=profile!='os';nano=profile.endswith('nano')
    flags=['-std=gnu11','-mcpu=cortex-m33','-mthumb','-mfloat-abi=softfp','-mfpu=fpv5-sp-d16',
           '-Os','-ffunction-sections','-fdata-sections','-ffp-contract=off','-w',
           '-Wno-error=incompatible-pointer-types','-Wno-error=int-conversion','-Werror=implicit-function-declaration',
           '-include',str(ROOT/'platform/rp2350/size_audit/config.h')]
    flags += ['-I'+str(ROOT/x) for x in ['platform/macos','platform/rp2350','QuakeMG24/Quake','QuakeMG24/src']]
    if lto:flags+=['-flto']
    if nano:flags+=['--specs=nano.specs']
    sources=sorted(s for s in (ROOT/'QuakeMG24/Quake').glob('*.c') if s.stem not in EXCLUDE)
    sources += [ROOT/'platform/macos'/x for x in ['game_sound.c','game_surface.c']]
    sources += [ROOT/'platform/rp2350'/x for x in ['qpak.c','qbsp.c','qmix.c']]
    def compile_one(source):
        obj=target/(source.stem+'.o');command=[gcc,*flags,'-c',source,'-o',obj]
        run(command,ROOT,target/(source.stem+'.log'))
        return {'source':str(source.relative_to(ROOT)),'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'object':str(obj)}
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:objects=list(pool.map(compile_one,sources))
    (target/'sources.json').write_text(json.dumps(objects,indent=2)+'\n')
    command=platform_compile.copy();command[command.index('-c')+1]=str(ROOT/'platform/rp2350/size_audit/platform.c')
    command[command.index('-o')+1]=str(target/'platform.o')
    command += [x for x in flags if x != "--specs=nano.specs" or x not in command]
    run(command,sdk,target/'platform.log')
    removed={'main.c.o','qbsp.c.o','qpak.c.o','qmix.c.o','qfiles.c.o','qrender.c.o','qcollision.c.o'}
    link=[x for x in base_link if not (x.endswith('.o') and Path(x).name in removed)]
    elf=target/'SIZE_ONLY_DO_NOT_FLASH.elf'
    link[link.index('-o')+1]=str(elf)
    link=[x if not x.startswith('-Wl,-Map=') else '-Wl,-Map='+str(target/'audit.map') for x in link]
    link+=['-Wl,--defsym=RAM_LENGTH=0x08000000','-Wl,--defsym=SCRATCH_X_ORIGIN=0x28000000',
           '-Wl,--defsym=SCRATCH_Y_ORIGIN=0x28001000',str(target/'platform.o')]
    link += [x['object'] for x in objects]
    if lto:link+=['-flto']
    if nano:
        if '--specs=nano.specs' not in link:link+=['--specs=nano.specs']
        link+=['-Wl,-u,_printf_float','-Wl,-u,_scanf_float']
    (target/'link-command.json').write_text(json.dumps(link,indent=2)+'\n')
    run(link,sdk,target/'link.log')
    nm=subprocess.check_output([str(gcc.with_name('arm-none-eabi-nm')),'-n',str(elf)],text=True)
    symbols={p[2]:int(p[0],16) for line in nm.splitlines() if len(p:=line.split())==3}
    start=symbols['__flash_binary_start'];end=symbols['__flash_binary_end'];size=end-start
    binary=elf.with_suffix('.bin')
    subprocess.run([str(gcc.with_name('arm-none-eabi-objcopy')),'-O','binary',str(elf),str(binary)],check=True)
    if binary.stat().st_size!=size:raise ValueError('Binary length and Flash load span disagree')
    sections=subprocess.check_output([str(gcc.with_name('arm-none-eabi-size')),'-A',str(elf)],text=True)
    (target/'sections.txt').write_text(sections)
    result={'profile':profile,'flash_load_bytes':size,'flash_kib':size/1024,'flash_start':hex(start),'flash_end':hex(end),
            'headroom_500000_bytes':500000-size,'headroom_500_kib':500*1024-size,'headroom_512_kib':512*1024-size,
            'headroom_640_kib_with_4k_guard':640*1024-4096-size,
            'headroom_768_kib_with_4k_guard':768*1024-4096-size,
            'engine_modules':len(objects),'elf':str(elf),'flags':flags,'sha256':hashlib.sha256(binary.read_bytes()).hexdigest()}
    print(f'{profile}: {size:,} bytes ({size/1024:.2f} KiB)',flush=True)
    return result

def check_limit(out,row,sdk,capacity,label):
    target=out/label;target.mkdir(exist_ok=True)
    (target/'pico_flash_region.ld').write_text(f'FLASH(rx) : ORIGIN = 0x10000000, LENGTH = {capacity}\n')
    link=json.loads((out/row['profile']/'link-command.json').read_text())
    link.insert(1,'-Wl,-L'+str(target))
    link[link.index('-o')+1]=str(target/'SIZE_ONLY_DO_NOT_FLASH.elf')
    link=[x if not x.startswith('-Wl,-Map=') else '-Wl,-Map='+str(target/'audit.map') for x in link]
    result=subprocess.run(link,cwd=sdk,capture_output=True,text=True)
    (target/'link.log').write_text(result.stdout+result.stderr)
    expected_fit=row['flash_load_bytes']<=capacity
    if (result.returncode==0)!=expected_fit:raise ValueError(f'Unexpected partition test outcome: {target}')
    if not expected_fit and 'overflowed by' not in result.stderr:raise ValueError('Link failed for a reason other than Flash overflow')
    return dict(profile=row['profile'],capacity_bytes=capacity,linked=result.returncode==0,
                diagnostics=[x for x in result.stderr.splitlines() if 'FLASH' in x],log=str(target/'link.log'))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pico8c-root',type=Path,default=ROOT.parent/'pico8c')
    p.add_argument('-o','--output',type=Path,default=ROOT/'build-host/rp2350-firmware-size')
    p.add_argument('--profiles',nargs='+',choices=['os','os-lto','os-lto-nano'],default=['os','os-lto','os-lto-nano'])
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);pico=a.pico8c_root.resolve()
    candidates=list((pico/'third_party/toolchains').glob('*/bin/arm-none-eabi-gcc'))
    if len(candidates)!=1:raise ValueError('Expected one ARM GCC installation in pico8c')
    gcc=candidates[0];sdk_cache={};rows=[]
    for profile in a.profiles:
        nano=profile.endswith('nano')
        if nano not in sdk_cache:sdk_cache[nano]=sdk_commands(out,pico,gcc,nano)
        rows.append(build(profile,out,gcc,*sdk_cache[nano]))
    best=min(rows,key=lambda r:r['flash_load_bytes'])
    checks=[check_limit(out,best,sdk_cache[best['profile'].endswith('nano')][0],500*1024,'limit-500KiB')]
    checks.append(check_limit(out,best,sdk_cache[best['profile'].endswith('nano')][0],640*1024-4096,'limit-640KiB-with-guard'))
    largest=max(rows,key=lambda r:r['flash_load_bytes'])
    checks.append(check_limit(out,largest,sdk_cache[largest['profile'].endswith('nano')][0],768*1024-4096,'limit-768KiB-with-guard'))
    report={'status':'flash_code_size_audit_not_bootable_firmware',
            'compiler':subprocess.check_output([str(gcc),'--version'],text=True).splitlines()[0],
            'sdk_version_source':(pico/'third_party/pico-sdk/pico_sdk_version.cmake').read_text(),
            'profile':'Full current MG24 portable C game, native ARM pointers, SDK default softfp ABI',
            'included':['Host_Frame and local server/client','Compiled Quake game logic and field access tables',
                        'MG24 world/alias/sprite/particle rendering, HUD, console and menus',
                        'QXIP readers and ADPCM mixer','Real core-1 display/DMA/PWM/I2C service and USB stdio',
                        'SDK startup, C/math libraries and initialized-data Flash load images'],
            'limitations':['Resource binder is a fail-fast placeholder; no target-native resource image is linked.',
                           'RAM/scratch placements are synthetic because host Flash simulation arrays remain in BSS.',
                           'Input/video boundary is a size-audit adapter, not a hardware-tested full port.',
                           'Legacy loading/diagnostic code remains; later removal may shrink the executable.',
                           'No UF2 is generated; do not flash these audit artifacts.'],
            'profiles':rows,'partition_link_checks':checks,'flash_partition_modified':False}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(out/'report.json')
if __name__=='__main__':main()
