"""Check the tracked publication surface; does not build or run Unreal."""
import ast
import json
import pathlib
import re
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
files = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root).decode().split('\0')
forbidden_dirs = {'artifacts', 'tools', 'node_modules', 'Binaries', 'Intermediate', 'Saved', 'Content', 'DerivedDataCache', '__pycache__', 'obj', 'bin'}
forbidden_suffixes = {'.dll', '.exe', '.pdb', '.lib', '.a', '.o', '.wasm', '.wcs', '.spv', '.uasset', '.umap', '.pak', '.utoc', '.ucas', '.pyc'}
secret = re.compile(r'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|^\s*(?:SecurityToken|Password|ApiKey|AccessToken)\s*=\s*[^\s]+)', re.M | re.I)
count = 0
for name in filter(None, files):
    relative = pathlib.PurePosixPath(name)
    assert not forbidden_dirs.intersection(relative.parts), f'Generated/external directory tracked: {name}'
    assert relative.suffix.lower() not in forbidden_suffixes, f'Binary/engine artifact tracked: {name}'
    assert not (name.startswith('web/generated/') or name.startswith('.env')), f'Local/generated file tracked: {name}'
    path = root / name
    assert not path.is_symlink(), f'External link tracked: {name}'
    data = path.read_bytes()
    assert len(data) < 1024 * 1024 and b'\0' not in data, f'Unexpected large/binary file: {name}'
    content = data.decode('utf-8-sig')
    assert not secret.search(content), f'Possible credential in: {name}'
    if relative.suffix == '.py':
        ast.parse(content, filename=name)
    elif relative.suffix in {'.json', '.uproject'}:
        json.loads(content)
    elif relative.suffix in {'.js', '.mjs'}:
        subprocess.run(['node', '--check', str(path)], check=True, capture_output=True)
    count += 1
print(f'PASS: {count} tracked text files; publication exclusions, credential patterns, Python/JSON/JavaScript syntax')
print('Scope: source hygiene only. No Unreal build, GPU test, or full-export validation was run.')
