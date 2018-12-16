#!/usr/bin/env python

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
    'msvcrt_old.lib',
    'opengl32.lib',
    'user32.lib',
    'winmm.lib',
    'xinput.lib'
]

FIXED_OPTIONS = [
    '/CRINKLER', '/UNSAFEIMPORT', '/ORDERTRIES:10000', '/COMPMODE:SLOW', '/HASHSIZE:500', '/HASHTRIES:100',
    '/OVERRIDEALIGNMENTS', '/UNALIGNCODE',
    '/PRINT:IMPORTS', '/PRINT:MODELS', '/PRIORITY:IDLE', '/LIBPATH:libs'
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

print "Name\t\t Size\t Time"

for test in tests:
    argi = test.rindex('\t')
    name = test[0:argi].strip()
    if len(chosen) > 0 and name not in chosen:
        continue
    args = test[argi+1:].strip().split(' ')
    exefile = name+exefile_postfix+".exe"
    t0 = time.time()
    
    print test[0:argi],
    sys.stdout.flush()
    minsize = 99999

    cmdline = [crinkler_exe, "/OUT:"+exefile] + ['/REPORT:' + name + '.html'] + FIXED_OPTIONS + args + LIBS
    rval = subprocess.call(cmdline, stdout=logfile)
    if rval == 0:
        size = os.stat(exefile).st_size
        print "\t%5d" % size,
    else:
        print "\terror",
    sys.stdout.flush()

    t1 = time.time()
    print "\t%5d" % (t1 - t0)
