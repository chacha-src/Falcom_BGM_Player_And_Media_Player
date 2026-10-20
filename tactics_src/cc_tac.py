import subprocess, shutil, sys, os
os.chdir(os.path.join(os.path.dirname(__file__), '..'))
FXC = r'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe'
E = [('vs_5_0', 'VSS'), ('vs_5_0', 'VSK'), ('ps_5_0', 'PSM'), ('ps_5_0', 'PSO'), ('ps_5_0', 'PSOV'), ('vs_5_0', 'VSH'),
     ('ps_5_0', 'PSH'), ('vs_5_0', 'VSP'), ('ps_5_0', 'PSP'), ('vs_5_0', 'VSQ'), ('ps_5_0', 'PSSKY')]
ok = True
for prof, ent in E:
    out = 'shaders/cso/tac_%s.cso' % ent
    r = subprocess.run([FXC, '/nologo', '/T', prof, '/E', ent, '/O3', '/Fo', out, 'shaders/omake_tactics.hlsl'], capture_output=True, text=True)
    if r.returncode:
        ok = False
        print(ent, 'FAILED', r.stdout, r.stderr)
    else:
        shutil.copyfile(out, 'res/cso/tac_%s.cso' % ent.lower())
        print(ent, 'ok')
sys.exit(0 if ok else 1)
