"""Generate a sorted, read-only word/reading index; never contains personal data."""
from pathlib import Path
import struct

def build(source, target):
    rows=set();started=False
    for line in Path(source).read_text(encoding='utf-8').splitlines():
        if line.strip()=='...':started=True;continue
        if not started or not line or line.startswith('#'):continue
        parts=line.split('\t')
        if len(parts)<2:continue
        word,reading=parts[:2]
        if not (1<=len(word)<=16) or not all('\u3400'<=c<='\u9fff' for c in word):continue
        if not reading or len(reading)>128 or not all(c==' ' or 'a'<=c<='z' for c in reading):continue
        rows.add((word+'\t'+reading).encode('utf-8'))
    rows=sorted(rows);offset=12+(len(rows)+1)*4;offsets=[]
    for row in rows:offsets.append(offset);offset+=len(row)
    offsets.append(offset)
    Path(target).write_bytes(b'ESREAD01'+struct.pack('<I',len(rows))+struct.pack('<'+'I'*len(offsets),*offsets)+b''.join(rows))
    print(f'Reading index: {len(rows)} entries, {offset} bytes')
