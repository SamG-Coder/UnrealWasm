"""Build matching font/image dependencies for the existing Unreal Slate code."""
import concurrent.futures
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import tarfile
import urllib.request

ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=ROOT/'engine/artifacts/slate-dependencies'
SDK=ROOT/'engine/tools/emsdk'
PYTHON=SDK/'python/3.13.3_64bit/python.exe'
SPECS={
 'freetype':('freetype/freetype','VER-2-14-1','44bd69d1f0750603410cfd4f26f1c5523c5a3a087a2dfa8639b757dc7c6677be'),
 'zlib':('madler/zlib','v1.3','b5b06d60ce49c8ba700e0ba517fa07de80b5d4628a037f4be8ad16955be7a7c0'),
 'libpng':('pnggroup/libpng','v1.6.44','0ef5b633d0c65f780c4fced27ff832998e71478c13b45dfb6e94f23a82f64f7c'),
 'harfbuzz':('harfbuzz/harfbuzz','2.4.0','dc3132a479c8c4fa1c9dd09d433a3ab9b0d2f302f844a764d57faf1629bfb9c5')}

def main():
    OUT.mkdir(exist_ok=True)
    sources={}
    for name,(repo,tag,digest) in SPECS.items():
        archive=OUT/(name+'.tar.gz')
        url=f'https://codeload.github.com/{repo}/tar.gz/refs/tags/{tag}'
        if not archive.exists():urllib.request.urlretrieve(url,archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=digest:raise RuntimeError(name+' checksum mismatch')
        destination=OUT/name
        if not destination.exists():
            with tarfile.open(archive) as tar:tar.extractall(destination,filter='data')
        sources[name]=next(destination.iterdir())
    ft=sources['freetype'];z=sources['zlib'];png=sources['libpng'];hb=sources['harfbuzz']/'src'
    shutil.copyfile(png/'scripts/pnglibconf.h.prebuilt',png/'pnglibconf.h')
    icu=ROOT/'engine/artifacts/icu64/include'
    # Keep the established upstream AHEasing version and source, with one
    # consistent LP64 double ABI in the library and consuming module.
    ah=ROOT/'engine/artifacts/open-source-ports/AHEasing'
    ah.mkdir(parents=True,exist_ok=True)
    ah_hashes={'easing.c':'a00c1bcf052f8e22c9b07b7077ebcc6fb2ecad277768e47d58fb78466496cae5',
        'easing.h':'c083345282fe824ac674fc5f0513c55991c7c4e1b62660d301b08386547b83a9',
        'UNLICENSE':'f0510d3b9d1223c45e8fdd2b49270ec8734fff491c7f7d1b948ee70162e1ceff'}
    for name,digest in ah_hashes.items():
        file=ah/name
        if not file.exists():
            relative=name if name=='UNLICENSE' else 'AHEasing/'+name
            urllib.request.urlretrieve('https://raw.githubusercontent.com/warrenm/AHEasing/82d85f7183e207bf4405d427a723a8a85406ddf4/'+relative,file)
        if hashlib.sha256(file.read_bytes()).hexdigest()!=digest:raise RuntimeError('AHEasing checksum mismatch: '+name)
    include=OUT/'include';include.mkdir(exist_ok=True)
    shutil.copytree(ft/'include',include/'freetype2',dirs_exist_ok=True)
    for name,source,pattern in [('zlib',z,'*.h'),('png',png,'*.h'),('harfbuzz',hb,'*.h'),('AHEasing',ah,'*.h')]:
        dest=include/name;dest.mkdir(exist_ok=True)
        for header in source.glob(pattern):shutil.copyfile(header,dest/header.name)
    shutil.copytree(z/'contrib/minizip',include/'zlib/minizip',dirs_exist_ok=True)
    ft_sources='''builds/unix/ftsystem.c src/autofit/autofit.c
src/base/ftbase.c src/base/ftbbox.c src/base/ftbdf.c src/base/ftbitmap.c src/base/ftcid.c
src/base/ftdebug.c src/base/ftfstype.c src/base/ftgasp.c src/base/ftglyph.c src/base/ftgxval.c
src/base/ftinit.c src/base/ftmm.c src/base/ftotval.c src/base/ftpatent.c src/base/ftpfr.c
src/base/ftstroke.c src/base/ftsynth.c src/base/fttype1.c src/base/ftwinfnt.c src/bdf/bdf.c
src/bzip2/ftbzip2.c src/cache/ftcache.c src/cff/cff.c src/cid/type1cid.c src/gzip/ftgzip.c
src/lzw/ftlzw.c src/pcf/pcf.c src/pfr/pfr.c src/psaux/psaux.c src/pshinter/pshinter.c
src/psnames/psnames.c src/raster/raster.c src/sdf/sdf.c src/sfnt/sfnt.c src/smooth/smooth.c
src/svg/svg.c src/truetype/truetype.c src/type1/type1.c src/type42/type42.c src/winfonts/winfnt.c'''.split()
    z_sources='adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil'.split()
    makefile=(hb/'Makefile.sources').read_text()
    base=makefile.split('HB_BASE_sources =',1)[1].split('\n\n',1)[0]
    hb_sources=re.findall(r'\b[\w-]+\.cc\b',base)+['hb-fallback-shape.cc','hb-ft.cc','hb-icu.cc']
    groups={
      'z':([z/(f+'.c') for f in z_sources]+[z/'contrib/minizip'/f for f in ('ioapi.c','unzip.c','zip.c')],['-I'+str(z),'-DHAVE_UNISTD_H']),
      'png':([f for f in sorted(png.glob('png*.c')) if f.name!='pngtest.c'],['-I'+str(z),'-I'+str(png)]),
      'freetype':([ft/f for f in ft_sources],['-I'+str(ft/'include'),'-I'+str(z),'-I'+str(png),'-DFT2_BUILD_LIBRARY','-DFT_CONFIG_OPTION_SYSTEM_ZLIB','-DFT_CONFIG_OPTION_USE_PNG','-DHAVE_UNISTD_H','-DHAVE_FCNTL_H']),
      'harfbuzz':([hb/f for f in hb_sources],['-I'+str(hb),'-I'+str(ft/'include'),'-I'+str(icu),'-DHAVE_FREETYPE','-DHAVE_ROUND','-DHAVE_ICU','-DHAVE_ICU_BUILTIN','-DHAVE_FALLBACK','-DHB_NO_PRAGMA_GCC_DIAGNOSTIC_ERROR','-DHAVE_PTHREAD','-DHAVE_UNISTD_H','-DHAVE_SYS_MMAN_H','-DHAVE_MPROTECT','-DHAVE_ISATTY','-DU_STATIC_IMPLEMENTATION']),
      'aheasing':([ah/'easing.c'],['-I'+str(ah),'-DAH_EASING_USE_DBL_PRECIS'])}
    headers=hashlib.sha256()
    for source in [*sources.values(),icu,ah]:
        for header in sorted(source.rglob('*.h')):headers.update(header.read_bytes())
    report={'sources':{name:{'repository':repo,'tag':tag,'sha256':digest} for name,(repo,tag,digest) in SPECS.items()},'libraries':{}}
    report['aheasing']={'commit':'82d85f7183e207bf4405d427a723a8a85406ddf4','sourceSHA256':hashlib.sha256((ah/'easing.c').read_bytes()).hexdigest(),'AHFloat':'double'}
    objects=OUT/'objects';objects.mkdir(exist_ok=True)
    def compile_source(item):
        name,file,extra=item
        obj=objects/(name+'_'+file.name+'.o');stamp=obj.with_suffix('.sha256')
        cpp=file.suffix=='.cc'
        command=[str(PYTHON),str(SDK/'upstream/emscripten'/('em++.py' if cpp else 'emcc.py')),'-m64','-msimd128','-O2',*extra]
        if cpp:command+=['-std=c++17','-fno-exceptions','-fno-rtti']
        command+=['-c',str(file),'-o',str(obj)]
        digest=hashlib.sha256(file.read_bytes()+headers.digest()+json.dumps(command).encode()).hexdigest()
        if not(obj.exists() and stamp.exists() and stamp.read_text()==digest):
            log=obj.with_suffix('.log')
            with log.open('w',encoding='utf-8') as stream:result=subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT)
            if result.returncode:raise RuntimeError(str(log)+'\n'+log.read_text()[-5000:])
            stamp.write_text(digest)
        return obj
    for name,(files,flags) in groups.items():
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            built=list(pool.map(compile_source,[(name,file,flags) for file in files]))
        library=OUT/('lib'+name+'-wasm64.a');library.unlink(missing_ok=True)
        response=OUT/(name+'.rsp');response.write_text('\n'.join('"'+str(obj).replace('\\','/')+'"' for obj in built))
        subprocess.run([str(SDK/'upstream/bin/llvm-ar.exe'),'rcs',str(library),'@'+str(response)],check=True)
        report['libraries'][name]={'sourceFiles':len(files),'sha256':hashlib.sha256(library.read_bytes()).hexdigest()}
        print(name+': '+str(len(files))+' source files compiled',flush=True)
    (OUT/'build.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
