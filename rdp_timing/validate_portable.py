"""Build standalone C/C++ examples and check the host adapter's timing contract."""
import argparse, datetime, hashlib, json, os, shutil, subprocess
from pathlib import Path

HERE=Path(__file__).resolve().parent

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True,help='Fresh directory for executables and logs')
    p.add_argument('--reference-header',type=Path,help='Original rdp_timing.h for a separate-TU differential')
    p.add_argument('--cc',default='gcc');p.add_argument('--cxx',default='g++')
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    env=dict(os.environ)
    mingw=Path('C:/msys64/mingw64/bin')
    if mingw.is_dir():env['PATH']=str(mingw)+os.pathsep+env.get('PATH','')
    def run(command):
        result=subprocess.run(command,env=env,capture_output=True,text=True,timeout=120)
        if result.returncode:raise RuntimeError(str(command)+'\n'+result.stdout+result.stderr)
        return result.stdout
    results={}
    for mode,compiler,standard in [('c99',a.cc,'c99'),('cpp11',a.cxx,'c++11')]:
        cc=shutil.which(compiler,path=env['PATH']);assert cc,compiler
        strict=[cc,'-O2','-Wall','-Wextra','-Werror','-pedantic','-std='+standard]
        if mode=='cpp11':strict+=['-x','c++']
        example=out/(mode+'-example.exe')
        run(strict+[str(HERE/'example.c'),'-o',str(example)])
        sample=run([str(example)])
        short_span=out/(mode+'-short-span-checks.exe')
        run(strict+[str(HERE/'tests/short_span_vi_checks.c'),'-o',str(short_span)])
        short_span_result=run([str(short_span)])
        objects=[]
        if a.reference_header:
            ref=a.reference_header.resolve(strict=True)
            obj=out/(mode+'-reference.o')
            run(strict+['-Wno-unused-function','-Wno-unused-but-set-variable',
                '-DRDP_TIMING_REFERENCE_HEADER="'+ref.as_posix()+'"',
                '-c',str(HERE/'tests/reference_bridge.c'),'-o',str(obj)])
            objects=[str(obj)]
        test=out/(mode+'-checks.exe')
        command=strict+(['-DRDP_TIMING_DIFFERENTIAL'] if objects else [])
        # -x c++ applies to source, not the separately compiled object file.
        if mode=='cpp11':
            source_obj=out/(mode+'-checks.o')
            run(command+['-c',str(HERE/'tests/portable_checks.c'),'-o',str(source_obj)])
            run([cc,str(source_obj)]+objects+['-o',str(test)])
        else:run(command+[str(HERE/'tests/portable_checks.c')]+objects+['-o',str(test)])
        checks=run([str(test)])+short_span_result
        (out/(mode+'.log')).write_text(sample+checks)
        results[mode]={'example':sample.strip(),'checks':checks.strip(),
                       'compiler':run([cc,'--version']).splitlines()[0]}
    source=HERE.parent/'src/device/rcp/rdp/rdp_timing.h'
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    # The standalone copy carries its existing license annotation.
    body=lambda path:path.read_text().replace('requirements. \n * License: WTFPL */','requirements. */')
    if source.exists():assert body(source)==body(HERE/'rdp_timing.h')
    result={'date':datetime.date.today().isoformat(),'timezone':'Australia/Sydney','results':results,
            'model_sha256':digest(HERE/'rdp_timing.h'),
            'adapter_sha256':digest(HERE/'rdp_timing_adapter.h'),
            'reference_sha256':digest(a.reference_header) if a.reference_header else None,
            'scope':'Standalone API and model-preservation checks; no new hardware accuracy claim.'}
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    (HERE/'VALIDATION.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
