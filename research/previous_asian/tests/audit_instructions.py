#!/usr/bin/env python3
import re, subprocess, sys
text=subprocess.check_output(['objdump','-d','-M','intel',sys.argv[1]],text=True)
names=('vpermi2ps','vpermt2ps','vgatherdps','vfmadd','vscalefps','vrndscaleps')
counts={n:len(re.findall(r'\b'+n,text)) for n in names}
print(' '.join(f'{k}={v}' for k,v in counts.items()))
if counts['vpermi2ps']+counts['vpermt2ps']==0: raise SystemExit('missing coefficient/final-z permutations')
if counts['vgatherdps']!=0: raise SystemExit('hot coefficient gather detected')
