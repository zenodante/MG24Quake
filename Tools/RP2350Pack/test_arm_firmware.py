#!/usr/bin/env python3
"""Execute the linked ARM firmware with peripherals intercepted, using Unicorn.
This tests CPU-side engine/resource behavior; it is not a hardware timing test.
Dependencies: unicorn, pyelftools (test tooling only).
"""
import argparse,json,struct,math,time,zlib,hashlib
from pathlib import Path
from unicorn import *
from unicorn.arm_const import *
from elftools.elf.elffile import ELFFile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf',type=Path);p.add_argument('assets',type=Path);p.add_argument('--frames',type=int,default=60);p.add_argument('--fault-test',action='store_true');p.add_argument('--tick-us',type=int,default=16667);p.add_argument('--teleport-test',choices=['easy','normal','hard']);p.add_argument('--input-matrix',action='store_true');p.add_argument('--buttons',action='store_true');p.add_argument('--trace-commands',action='store_true');p.add_argument('--capture',type=Path);p.add_argument('--cycle',type=int,default=0);p.add_argument('--json',type=Path);a=p.parse_args()
elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest();asset_sha256=hashlib.sha256(a.assets.read_bytes()).hexdigest()
u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
u.mem_map(0x10000000,0x1000000);u.mem_map(0x20000000,0x82000);u.mem_map(0xe0000000,0x100000)
with a.elf.open('rb') as f:
    e=ELFFile(f);symbols={x.name:x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols()}
    for seg in e.iter_segments():
        if seg['p_type']=='PT_LOAD':u.mem_write(seg['p_vaddr'],seg.data())
if symbols['textureCacheBuffer']%8:
    raise SystemExit(f"Misaligned typed scratch buffer: textureCacheBuffer={symbols['textureCacheBuffer']:#x}; requires 8-byte alignment")
u.mem_write(0x100c0000,a.assets.read_bytes());u.mem_protect(0x10000000,0x1000000,UC_PROT_READ|UC_PROT_EXEC)
u.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);u.mem_write(0xe000ed88,struct.pack('<I',0xf00000))
u.reg_write(UC_ARM_REG_SP,symbols['__StackTop']);u.reg_write(UC_ARM_REG_LR,0x10000001)
regs=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]
frames=0;clock=0;commands=[];map_stats={};input_checks=[];weapon_impulses=0;
matrix=['in_moveright','in_attack',None,'in_moveleft','in_forward','in_back','in_left','in_right','in_lookup','in_lookdown'];
start=time.time();failure=None
clipped_submodel_calls=0
teleport_events=0;teleport_destination=False;player_pointer=None;player_origin=None;teleport_functions=[]

def string(ptr):
    b=bytearray()
    for i in range(1024):
        v=u.mem_read(ptr+i,1)[0]
        if not v:break
        b.append(v)
    return b.decode('utf8','replace')
def ret(value=0):u.reg_write(UC_ARM_REG_R0,value&0xffffffff);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
def hook(name,fn):
    if name in symbols:
        at=symbols[name]&~1;u.hook_add(UC_HOOK_CODE,lambda uc,addr,size,user:fn(),begin=at,end=at)
def error():
    global failure
    failure=string(u.reg_read(UC_ARM_REG_R0));print('SYS_ERROR:',failure,[hex(u.reg_read(r)) for r in regs],flush=True);u.emu_stop()
def tick():
    global clock
    clock+=a.tick_us;u.reg_write(UC_ARM_REG_R1,0);ret(clock)
def acquire():
    u.mem_write(u.reg_read(UC_ARM_REG_R0),b'\0'*4);ret(symbols['frame']+4)
def submit():
    global frames
    frames+=1
    vals=struct.unpack('<9I32s',u.mem_read(symbols['qrp_game_stats'],68));mapname=vals[-1].split(b'\0')[0].decode()
    if vals[1]==4 and vals[8]:map_stats[mapname]=dict(signon=vals[1],entities=vals[2],health=vals[3],zone_peak=vals[4],metadata_peak=vals[5],shells=vals[6],sound_starts=vals[7])
    if a.input_matrix and frames>=20:
        index,phase=divmod(frames-20,10)
        if index<len(matrix) and phase in (3,7) and matrix[index]:
            held=bool(u.mem_read(symbols[matrix[index]]+2,1)[0]&1)
            input_checks.append(dict(button=index,action=matrix[index],phase=phase,held=held,passed=held==(phase==3)))
    if a.teleport_test:
        if frames==12:commands.append('+forward')
        if teleport_destination and frames%10==0:commands.append('-forward')
    if a.cycle:
        maps=['start','e1m1','e1m2','e1m3','e1m4','e1m5','e1m6','e1m7','e1m8']
        phase=frames%a.cycle
        if phase==0 and frames//a.cycle<len(maps):commands.append('map '+maps[frames//a.cycle])
        if phase==20:commands.extend(['+attack','+forward'])
        if phase==45:commands.extend(['-attack','-forward'])
    if frames%10==0:print('frame',frames,'elapsed',round(time.time()-start,1),flush=True)
    if frames>=a.frames:
        if a.capture:
            pixels=u.mem_read(symbols['frame']+4,64000);pal=u.mem_read(u.reg_read(UC_ARM_REG_R1),768)
            scan=b''.join(b'\0'+b''.join(pal[v*3:v*3+3] for v in pixels[y*320:(y+1)*320]) for y in range(200))
            def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b))
            a.capture.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',320,200,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))
        u.emu_stop()
    elif a.fault_test and frames==20:
        # Simulate exception entry with an identifiable stacked PC/LR. The real
        # naked handler must switch to its emergency stack and publish a report.
        sp=symbols['__StackTop']-64
        u.mem_write(sp,struct.pack('<8I',0,0,0,0,0,0x10001235,0x10005678,0x01000000))
        u.reg_write(UC_ARM_REG_SP,sp);u.reg_write(UC_ARM_REG_LR,0xfffffff9)
        u.reg_write(UC_ARM_REG_PC,symbols['isr_hardfault'])
    else:ret()
def crc():ret(zlib.crc32(u.mem_read(u.reg_read(UC_ARM_REG_R0),u.reg_read(UC_ARM_REG_R1))))
for name in ['stdio_init_all','qservice_start','qservice_sound_start','qservice_sound_gain','qservice_sound_stop','qservice_sound_stop_all','qservice_sound_fence_done']:hook(name,lambda:ret(1))
for name in ['__wrap_printf','__wrap_vprintf','__wrap_puts','__wrap_putchar','qservice_buttons_pressed']+([] if (a.buttons or a.input_matrix) else ['qservice_buttons']):hook(name,lambda:ret())
def console():
    if commands:
        u.mem_write(0x20077000,commands.pop(0).encode()+b'\n\0');ret(0x20077000)
    else:ret()
hook('Sys_ConsoleInput',console)
if a.trace_commands:
    for name in ['Cbuf_AddText','Cbuf_InsertText','Cmd_ExecuteString','SV_SpawnServer']:
        hook(name,lambda name=name:print(name,string(u.reg_read(UC_ARM_REG_R0)),flush=True) if frames<10 else None)

if a.input_matrix:
    def buttons():
        index,phase=divmod(frames-20,10)
        ret(1<<index if 0<=index<10 and phase<4 else 0)
    def impulse():
        global weapon_impulses
        weapon_impulses+=1
    hook('qservice_buttons',buttons);hook('IN_Impulse',impulse)
elif a.buttons:hook('qservice_buttons',lambda:ret(18 if 20<=frames<45 else 0))
def clipped_submodel():
    global clipped_submodel_calls
    clipped_submodel_calls+=1
hook('R_DrawSolidClippedSubmodelPolygons',clipped_submodel)
if a.teleport_test:
    def origin():
        global player_pointer,player_origin,teleport_destination
        v=struct.unpack('<3f',struct.pack('<2I',u.reg_read(regs[2]),u.reg_read(regs[3]))+bytes(u.mem_read(u.reg_read(UC_ARM_REG_SP),4)))
        if player_pointer is None and v==(544.0,288.0,33.0):
            player_pointer=u.reg_read(regs[1]);v=({'easy':232.0,'normal':544.0,'hard':864.0}[a.teleport_test],1300.0,32.0)
            for r,val in zip(regs[2:],struct.unpack('<3I',struct.pack('<3f',*v))):u.reg_write(r,val)
            u.mem_write(u.reg_read(UC_ARM_REG_SP),struct.pack('<f',v[2]))
            print('spawn positioned before skill portal',v,flush=True)
        if u.reg_read(regs[1])==player_pointer:
            player_origin=v
            if v[1]>=1500:teleport_destination=True
    def trace_teleport(name):
        global teleport_events
        if name=='qcc_teleport_touch':teleport_events+=1
        teleport_functions.append(name);print('teleport trace:',name,'frame',frames,flush=True)
    hook('set_qcc_origin',origin)
    for name in ['qcc_teleport_touch','qcc_spawn_tfog','qcc_spawn_tdeath','R_TeleportSplash']:
        hook(name,lambda name=name:trace_teleport(name))
if a.fault_test:
    def fault_published(uc,access,address,size,value,user):
        global failure
        if address==symbols['qrp_diagnostic'] and value==1:
            values=struct.unpack('<8I',u.mem_read(symbols['qrp_diagnostic'],32))
            if values[1:4]!=(0x10005678,0x10001235,symbols['__StackTop']-64):
                failure='Incorrect fault record: '+repr(values)
            u.emu_stop()
    u.hook_add(UC_HOOK_MEM_WRITE,fault_published,begin=symbols['qrp_diagnostic'],end=symbols['qrp_diagnostic']+3)
hook('Sys_Error',error);hook('panic',error);hook('qservice_frame_acquire',acquire);hook('qservice_frame_submit',submit);hook('time_us_64',tick);hook('qpak_crc32',crc)
# Replace RP2350 DCP double helpers with IEEE operations; single precision game
# arithmetic remains executed as ARM VFP instructions.
def dget(i):return struct.unpack('<d',struct.pack('<II',u.reg_read(regs[i]),u.reg_read(regs[i+1])))[0]
def dret(v):
    lo,hi=struct.unpack('<II',struct.pack('<d',v));u.reg_write(UC_ARM_REG_R1,hi);ret(lo)
for op,fn in [('add',lambda x,y:x+y),('sub',lambda x,y:x-y),('rsub',lambda x,y:y-x),('mul',lambda x,y:x*y),('div',lambda x,y:x/y if y else math.copysign(math.inf,x) if x else math.nan)]:hook('__wrap___aeabi_d'+op,lambda fn=fn:dret(fn(dget(0),dget(2))))
for op,fn in [('eq',lambda x,y:x==y),('lt',lambda x,y:x<y),('le',lambda x,y:x<=y),('gt',lambda x,y:x>y),('ge',lambda x,y:x>=y),('un',lambda x,y:math.isnan(x) or math.isnan(y))]:hook('__wrap___aeabi_dcmp'+op,lambda fn=fn:ret(int(fn(dget(0),dget(2)))))
hook('__wrap___aeabi_i2d',lambda:dret(float(struct.unpack('<i',struct.pack('<I',u.reg_read(regs[0])))[0])))
hook('__wrap___aeabi_ui2d',lambda:dret(float(u.reg_read(regs[0]))))
hook('__wrap___aeabi_d2iz',lambda:ret(int(dget(0))))
hook('__wrap___aeabi_d2uiz',lambda:ret(int(dget(0))))
hook('__wrap___aeabi_d2f',lambda:ret(struct.unpack('<I',struct.pack('<f',dget(0)))[0]))
for name in ['sqrt','sin','cos','tan','atan','floor','ceil','trunc','exp','log']:
    hook('__wrap_'+name,lambda name=name:dret(float(getattr(math,name)(dget(0)))))
try:u.emu_start(symbols['main']|1,0,timeout=0,count=12_000_000_000)
except UcError as err:
    pc=u.reg_read(UC_ARM_REG_PC);near=sorted((v,k) for k,v in symbols.items() if v<=pc)[-5:]
    failure=f'{err}; PC={pc:#x}; nearest={near}';print(failure,flush=True)
if a.fault_test and failure is None and frames==20:
    diagnostic_rows=[]
    for name in ['st7789_dma_wait','st7789_end_write','st7789_set_window','st7789_start_write']:
        hook(name,lambda:ret())
    def diagnostic_write():
        diagnostic_rows.append(bytes(u.mem_read(u.reg_read(regs[0]),u.reg_read(regs[1]))));ret()
    hook('st7789_write_dma',diagnostic_write)
    u.hook_add(UC_HOOK_CODE,lambda *args:u.emu_stop(),begin=0x10000000,end=0x10000000)
    u.reg_write(UC_ARM_REG_SP,symbols['__StackTop']);u.reg_write(UC_ARM_REG_LR,0x10000001)
    u.emu_start(symbols['qrp_diagnostic_display']|1,0,count=10_000_000)
    if len(diagnostic_rows)!=200 or any(len(r)!=640 for r in diagnostic_rows) or not any(b'\xff\xff' in r for r in diagnostic_rows):
        failure='Fault display did not produce 200 RGB565 scanlines'
    print('fault display scanlines:',len(diagnostic_rows),flush=True)
matrix_passed=not a.input_matrix or (len(input_checks)==18 and all(x['passed'] for x in input_checks) and weapon_impulses==1)
result=dict(texture_scratch_address=hex(symbols['textureCacheBuffer']),clipped_submodel_calls=clipped_submodel_calls,teleport_events=teleport_events,teleport_destination=teleport_destination,player_origin=player_origin,teleport_functions=teleport_functions,input_checks=input_checks,weapon_impulses=weapon_impulses,test='ARM engine with intercepted peripherals',elf_sha256=elf_sha256,asset_sha256=asset_sha256,frames=frames,elapsed_seconds=time.time()-start,failure=failure,maps=map_stats,passed=(not a.teleport_test or (teleport_events>0 and teleport_destination)) and matrix_passed and frames>=(20 if a.fault_test else a.frames) and failure is None and (not a.cycle or len([k for k in map_stats if k])==min(9,a.frames//a.cycle)))
print(json.dumps(result,indent=2))
if a.json:a.json.write_text(json.dumps(result,indent=2)+'\n')
raise SystemExit(0 if result['passed'] else 1)
