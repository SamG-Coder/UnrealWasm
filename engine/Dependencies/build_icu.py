"""Reproducible ICU 64.1 Wasm64 build matching UE's callback ABI and data."""
import concurrent.futures
import hashlib
import json
import pathlib
import shutil
import subprocess
import urllib.request
import zipfile

ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=ROOT/'engine/artifacts/icu64'
SDK=ROOT/'engine/tools/emsdk'
PYTHON=SDK/'python/3.13.3_64bit/python.exe'
EM=SDK/'upstream/emscripten/em++.py'
URL='https://github.com/unicode-org/icu/releases/download/release-64-1/icu4c-64_1-src.zip'
SHA256='c927c4a07aa235596dfbd039d86c08dc3c98b070347920ec66078238f6e27295'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    archive=OUT/'icu4c-64_1-src.zip'
    if not archive.exists():urllib.request.urlretrieve(URL,archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=SHA256:raise RuntimeError('ICU source archive checksum mismatch')
    upstream=OUT/'upstream/icu/source'
    if not upstream.exists():
        with zipfile.ZipFile(archive) as source:source.extractall(OUT/'upstream')
    source=OUT/'source'
    for part in ('common','i18n','io','stubdata'):
        shutil.copytree(upstream/part,source/part,dirs_exist_ok=True)
    header=source/'common/unicode/uclean.h'
    text=header.read_text(encoding='utf-8')
    hook='''typedef UBool U_CALLCONV UDataFileOpenFn(const void*, void**, void**, const char*);
typedef void U_CALLCONV UDataFileCloseFn(const void*, void* const, void* const);
U_CAPI void U_EXPORT2 u_setDataFileFunctions(const void*, UDataFileOpenFn*, UDataFileCloseFn*, UErrorCode*);
'''
    assert 'U_CDECL_END' in text
    header.write_text(text.replace('U_CDECL_END',hook+'\nU_CDECL_END',1),encoding='utf-8')
    rename=source/'common/unicode/urename.h'
    text=rename.read_text(encoding='utf-8')
    rename.write_text(text.replace('#define u_setMemoryFunctions ', '#define u_setDataFileFunctions U_ICU_ENTRY_POINT_RENAME(u_setDataFileFunctions)\n#define u_setMemoryFunctions '),encoding='utf-8')
    adapter=ROOT/'engine/Dependencies/icu-file-adapter.cpp'
    shutil.copyfile(adapter,source/'common/umapfile.cpp')
    include=OUT/'include/unicode'
    include.mkdir(parents=True,exist_ok=True)
    for part in ('common','i18n','io'):
        shutil.copytree(source/part/'unicode',include,dirs_exist_ok=True)
    headers=hashlib.sha256()
    for header in sorted(source.rglob('*.h')):headers.update(header.read_bytes())
    flags=['-m64','-msimd128','-O2','-std=c++17','-fno-exceptions','-frtti',
        '-DU_STATIC_IMPLEMENTATION','-DU_USING_ICU_NAMESPACE=0','-DU_NO_DEFAULT_INCLUDE_UTF_HEADERS=1',
        '-DUNISTR_FROM_CHAR_EXPLICIT=explicit','-DUNISTR_FROM_STRING_EXPLICIT=explicit','-DUCONFIG_NO_TRANSLITERATION=1',
        '-Wno-deprecated-declarations','-Wno-deprecated-anon-enum-enum-conversion','-I'+str(source/'common')]
    objects=OUT/'objects';objects.mkdir(exist_ok=True)
    def compile_file(item):
        part,file=item
        obj=objects/(part+'_'+file.name+'.o'); stamp=obj.with_suffix('.sha256')
        command=[str(PYTHON),str(EM),*flags,'-I'+str(source/part)]
        if part!='stubdata':command+=['-DU_'+{'common':'COMMON','i18n':'I18N','io':'IO'}[part]+'_IMPLEMENTATION=1']
        command+=['-c',str(file),'-o',str(obj)]
        digest=hashlib.sha256(file.read_bytes()+headers.digest()+json.dumps(command).encode()).hexdigest()
        if not(obj.exists() and stamp.exists() and stamp.read_text()==digest):
            log=obj.with_suffix('.log')
            with log.open('w',encoding='utf-8') as stream:
                result=subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT)
            if result.returncode:raise RuntimeError(str(log)+'\n'+log.read_text()[-5000:])
            stamp.write_text(digest)
        return obj
    inputs=[(part,file) for part in ('common','i18n','io','stubdata') for file in sorted((source/part).glob('*.cpp'))]
    built=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for index,obj in enumerate(pool.map(compile_file,inputs),1):
            built.append(obj)
            if index%25==0 or index==len(inputs):print(f'ICU {index}/{len(inputs)} compiled',flush=True)
    library=OUT/'libicu64-wasm64.a'
    # Recreate this single generated archive; stale members must not survive.
    library.unlink(missing_ok=True)
    response=OUT/'archive.rsp'
    response.write_text('\n'.join('"'+str(p).replace('\\','/')+'"' for p in built),encoding='utf-8')
    subprocess.run([str(SDK/'upstream/bin/llvm-ar.exe'),'rcs',str(library),'@'+str(response)],check=True)
    report={'version':'64.1','sourceURL':URL,'sourceSHA256':SHA256,'sourceFiles':len(inputs),
        'fileAdapterSHA256':hashlib.sha256(adapter.read_bytes()).hexdigest(),'librarySHA256':hashlib.sha256(library.read_bytes()).hexdigest(),
        'changes':['Added Unreal data-file callback declarations and symbol renaming','Replaced OS mapping backend with Unreal callback adapter'],
        'fullEngineLinked':False}
    (OUT/'build.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
