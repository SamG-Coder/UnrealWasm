"""Expand UBT's response using Windows argv rules for the Windows SDK RC tool."""
import ctypes
import pathlib
import subprocess
import sys

response = pathlib.Path(sys.argv[2]).read_text(encoding='utf-8-sig')
shell = ctypes.WinDLL('shell32', use_last_error=True)
parse = shell.CommandLineToArgvW
parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
count = ctypes.c_int()
argv = parse('rc.exe ' + ' '.join(response.splitlines()), ctypes.byref(count))
if not argv:
    raise ctypes.WinError(ctypes.get_last_error())
try:
    arguments = [argv[index] for index in range(1, count.value)]
finally:
    free = ctypes.WinDLL('kernel32').LocalFree
    free.argtypes = [ctypes.c_void_p]
    free.restype = ctypes.c_void_p
    free(argv)
sys.exit(subprocess.run([sys.argv[1], *arguments]).returncode)
