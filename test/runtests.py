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
    '/CRINKLER', '/UNSAFEIMPORT',
    '/OVERRIDEALIGNMENTS', '/UNALIGNCODE',
    '/PRINT:IMPORTS', '/PRINT:MODELS', '/PRIORITY:IDLE',
    '/LIBPATH:libs',
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

EXTRA_OPTIONS =  ['/COMPMODE:SLOW', '/ORDERTRIES:10000', '/HASHSIZE:500', '/HASHTRIES:1000',]

def runtests(crinkler_exe, testlist, logfile, chosen, makename, options, report = True, post = None):
    testlistfile = open(testlist, 'r')
    tests = testlistfile.readlines()
    testlistfile.close()

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

        cmdline = [crinkler_exe] + FIXED_OPTIONS + options + args + LIBS
        if report:
            cmdline += ['/REPORT:' + name + '.html']
        exefile = ""
        for s in cmdline:
            if s.startswith("/OUT:"):
                exefile = s.split(':')[1]
        
        if exefile == "":
            exefile = makename(name) + ".exe"
            cmdline += ["/OUT:"+exefile]

        rval = subprocess.call(cmdline, stdout=logfile, stderr=logfile)
        if rval == 0:
            size = os.stat(exefile).st_size
            print("\t%5d" % size, end='')
            if post:
                post(name)
        else:
            print("\terror", end='')
        sys.stdout.flush()

        t1 = time.time()
        print("\t%5d" % (t1 - t0))


if __name__ == "__main__":
    crinkler_exe = sys.argv[1]
    testlist = sys.argv[2]

    chosen = sys.argv[3:]

    if len(sys.argv) > 4:
        makename = lambda x: x + sys.argv[4]
    else:
        makename = lambda x: x

    logfilename = "testlog.txt"
    logfile = open(logfilename, "w")

    runtests(crinkler_exe, testlist, logfile, chosen, makename, EXTRA_OPTIONS)
