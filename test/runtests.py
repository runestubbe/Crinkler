#!/usr/bin/env python

from __future__ import print_function
import sys
import os
import subprocess
import time

LIBS = [
    'kernel32.lib',
    'd3d11.lib',
    'd3d9.lib',
    'd3dcompiler.lib',
    'd3dx9.lib',
    'dinput8.lib',
    'dsound.lib',
    'dxguid.lib',
    'gdi32.lib',
    'glu32.lib',
    'opengl32.lib',
    'user32.lib',
    'winmm.lib',
    'xinput.lib'
]

FIXED_OPTIONS = [
    '/CRINKLER', '/UNSAFEIMPORT', '/ORDERTRIES:10000', '/COMPMODE:SLOW', '/HASHSIZE:500', '/HASHTRIES:100',
    '/OVERRIDEALIGNMENTS', '/UNALIGNCODE',
    '/PRINT:IMPORTS', '/PRINT:MODELS', '/PRIORITY:IDLE',
    '/LIBPATH:""C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x86""',
    '/LIBPATH:""C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.19041.0\\um\\x86""',
    '/LIBPATH:""C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.18362.0\\um\\x86""',
    '/LIBPATH:""C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.17763.0\\um\\x86""',
    '/LIBPATH:""C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.17134.0\\um\\x86""',
    '/LIBPATH:""C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.16299.0\\um\\x86""',
    '/LIBPATH:""C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.15063.0\\um\\x86""',
    '/LIBPATH:""C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.14393.0\\um\\x86""',
    '/LIBPATH:""C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.10586.0\\um\\x86""',
    '/LIBPATH:""C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.10240.0\\um\\x86""',
    '/LIBPATH:""C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.10150.0\\um\\x86""',
    '/LIBPATH:""C:\\Program Files (x86)\\Microsoft DirectX SDK (June 2010)\\Lib\\x86""',
]


crinkler_exe = sys.argv[1]
testlist = sys.argv[2]

testlistfile = open(testlist, 'r')
tests = testlistfile.readlines()
testlistfile.close()

chosen = sys.argv[3:]

logfilename = "testlog.txt"
logfile = open(logfilename, "w")

if len(sys.argv) > 4:
    exefile_postfix = sys.argv[4]
else:
    exefile_postfix = ""

print("Name\t\t Size\t Time")

for test in tests:
    argi = test.rindex('\t')
    name = test[0:argi].strip()
    if len(chosen) > 0 and name not in chosen:
        continue
    args = test[argi+1:].strip().split(' ')
    t0 = time.time()
    
    print(test[0:argi], end='')
    sys.stdout.flush()
    minsize = 99999

    cmdline = [crinkler_exe] + ['/REPORT:' + name + '.html'] + FIXED_OPTIONS + args + LIBS
    exefile = ""
    for s in cmdline:
        if s.startswith("/OUT:"):
            exefile = s.split(':')[1]
    
    if exefile == "":
        exefile = name+exefile_postfix+".exe"
        cmdline += ["/OUT:"+exefile]

    rval = subprocess.call(cmdline, stdout=logfile)
    if rval == 0:
        size = os.stat(exefile).st_size
        print("\t%5d" % size, end='')
    else:
        print("\terror", end='')
    sys.stdout.flush()

    t1 = time.time()
    print("\t%5d" % (t1 - t0))
