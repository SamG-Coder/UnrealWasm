"""Offline CUDA compilation for a UE texture-using compute shader.

This is a compiler bring-up artifact, NOT a browser-loadable shader. Its CUDA
texture/surface handles and global parameter ABI are not implemented by the
browser dispatch queue yet. Keep this separate from the verified buffer profile.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess

RGB_STORAGE_ABI = r'''
// Backend storage adapter: CUDA has no native three-channel array element.
// RGB resources use RGBA32F physical storage; shader values remain float3.
template<> inline __device__ float3 tex2Dfetch_int<float3>(CUtexObject texObj, int x, int y, int mip) {
    float4 v = tex2Dfetch_int<float4>(texObj, x, y, mip);
    return make_float3(v.x, v.y, v.z);
}
__device__ void webcuda_store_rgb(float3 value, CUsurfObject surface, int logicalByteX, int y, int layer, cudaSurfaceBoundaryMode mode) {
    surf2DLayeredwrite<float4>(make_float4(value.x, value.y, value.z, 0.0f), surface, (logicalByteX / 12) * 16, y, layer, mode);
}
'''


def compile_shader(source, entry, out, slang, nvcc, ccbin):
    out.mkdir(parents=True, exist_ok=True)
    original = source.read_bytes()
    hlsl = original.decode('utf-8-sig')
    # Slang resolves types out of declaration order. Unlike DXC it interprets
    # some HLSL struct forwards as distinct types. Remove only forwards whose
    # actual complete definitions exist in the same translation unit.
    forwards = []
    def remove_forward(match):
        name = match.group(1)
        if not re.search(r'\bstruct\s+' + re.escape(name) + r'\s*\{', hlsl):
            raise ValueError('Incomplete struct ' + name)
        forwards.append(name)
        return '\n'
    lowered = re.sub(r'(?m)^\s*struct\s+(\w+)\s*;\s*$', remove_forward, hlsl)
    # Explicit zero texel offset is equivalent to the overload with no offset.
    # The identifier match deliberately excludes complex or nonzero expressions.
    lowered, zero_offsets = re.subn(r'\.Load\((\w+),\s*0\)', r'.Load(\1)', lowered)
    lowered_path = out / (entry + '.hlsl')
    lowered_path.write_text(lowered, encoding='utf-8')
    raw_path = out / (entry + '.slang.cu')
    reflection_path = out / (entry + '.reflection.json')
    command = [str(slang), '-lang', 'hlsl', str(lowered_path), '-entry', entry,
               '-stage', 'compute', '-target', 'cuda', '-o', str(raw_path),
               '-reflection-json', str(reflection_path)]
    compilation = subprocess.run(command, capture_output=True, text=True)
    (out / 'slang.log').write_text(compilation.stdout + compilation.stderr, encoding='utf-8')
    compilation.check_returncode()
    raw = raw_path.read_text()
    marker = '#line 1 "MoveShaderParametersToRootConstantBuffer"'
    if raw.count(marker) != 1:
        raise ValueError('Unexpected Slang prelude boundary')
    prelude, body = raw.split(marker, 1)
    body, rgb_stores = re.subn(r'surf2DLayeredwrite<float3\s*>', 'webcuda_store_rgb', body)
    rgba_cuda = prelude + RGB_STORAGE_ABI + '\n' + marker + body
    cuda_path = out / (entry + '.cu')
    cuda_path.write_text(rgba_cuda, encoding='utf-8')
    cubin_path = out / (entry + '.cubin')
    command = [str(nvcc), '-cubin', '-arch=sm_120', '--allow-unsupported-compiler',
               '-diag-suppress=1444', '-ccbin', str(ccbin), str(cuda_path), '-o', str(cubin_path)]
    compilation = subprocess.run(command, capture_output=True, text=True)
    (out / 'nvcc.log').write_text(compilation.stdout + compilation.stderr, encoding='utf-8')
    compilation.check_returncode()
    if source.read_bytes() != original:
        raise RuntimeError('Original Unreal shader changed')
    reflection = json.loads(reflection_path.read_text())
    manifest = {
        'schema': 1, 'profile': 'offline-cuda-texture-compile-only', 'entry': entry,
        'source': str(source.resolve()), 'sourceSHA256': hashlib.sha256(original).hexdigest(),
        'cudaSHA256': hashlib.sha256(cuda_path.read_bytes()).hexdigest(),
        'cubinSHA256': hashlib.sha256(cubin_path.read_bytes()).hexdigest(),
        'compiler': subprocess.check_output([str(slang), '-version'], stderr=subprocess.STDOUT, text=True).strip(),
        'removedCompleteTypeForwards': forwards, 'zeroOffsetOverloads': zero_offsets, 'rgbSurfaceStores': rgb_stores,
        'requiredRGBPhysicalStorage': 'RGBA32F', 'resources': reflection['parameters'],
        'architecture': 'sm_120', 'executed': False, 'browserLoadable': False,
        'remaining': ['CUDA texture/surface allocation and shader binding',
                      'All typed resource formats and samplers must match Unreal semantics',
                      'Global constant parameter packing and dispatch synchronization',
                      'Native GPU parity against captured Unreal render-graph inputs',
                      'Integration into Unreal ShaderCompileWorker and the custom RHI']}
    (out / (entry + '.json')).write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps({'entry': entry, 'cubinBytes': cubin_path.stat().st_size, 'executed': False}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=pathlib.Path)
    parser.add_argument('--entry', required=True)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--slang', type=pathlib.Path, default=pathlib.Path('C:/VulkanSDK/1.4.357.0/Bin/slangc.exe'))
    parser.add_argument('--nvcc', type=pathlib.Path, default=pathlib.Path('C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3/bin/nvcc.exe'))
    parser.add_argument('--ccbin', type=pathlib.Path, default=pathlib.Path('C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64'))
    args = parser.parse_args()
    compile_shader(args.source, args.entry, args.out, args.slang, args.nvcc, args.ccbin)
