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

#'/ORDERTRIES:10000', 

FIXED_OPTIONS = [
    '/CRINKLER', '/UNSAFEIMPORT', '/ORDERTRIES:10000', '/COMPMODE:SLOW', '/HASHSIZE:500', '/HASHTRIES:100',
    '/OVERRIDEALIGNMENTS', '/UNALIGNCODE',
    '/PRINT:IMPORTS', '/PRINT:MODELS', '/PRIORITY:IDLE', '/LIBPATH:libs'
]

VARYING_OPTIONS = [[], ['/TRANSFORM:CALLS'], ['/RANGE:opengl32', '/RANGE:d3dx9_43'], ['/TRANSFORM:CALLS', '/RANGE:opengl32', '/RANGE:d3dx9_43']]
VARYING_OPTION_NAMES = ['_', 'T', 'R', 'TR']

#VARYING_OPTIONS = [[], ['/TRANSFORM:CALLS']]
#VARYING_OPTION_NAMES = ['_', 'T']

#VARYING_OPTIONS = [[]]
#VARYING_OPTION_NAMES = ['_']


crinkler_exe = sys.argv[1]
testlist = sys.argv[2]

testlistfile = open(testlist, 'r')
tests = testlistfile.readlines()
testlistfile.close()

if len(sys.argv) > 3:
    logfilename = sys.argv[3]
else:
    logfilename = "testlog.txt"
logfile = open(logfilename, "w")

if len(sys.argv) > 4:
    exefile_postfix = sys.argv[4]
else:
    exefile_postfix = ""

print "Name\t",
for on in VARYING_OPTION_NAMES:
    print "\t%5s" % on,
print "\t  min\t time"

for test in tests:
    argi = test.rindex('\t')
    name = test[0:argi].strip()
    args = test[argi+1:].strip().split(' ')
    exefile = name+exefile_postfix+".exe"
    t0 = time.time()
    
    print test[0:argi],
    sys.stdout.flush()
    minsize = 99999
    for o in VARYING_OPTIONS:
        cmdline = [crinkler_exe, "/OUT:"+exefile] + ['/REPORT:' + name + '.html'] + FIXED_OPTIONS + o + args + LIBS
        rval = subprocess.call(cmdline, stdout=logfile)
        if rval == 0:
            size = os.stat(exefile).st_size
            minsize = min(size,minsize)
            print "\t%5d" % size,
        else:
            print "\terror",
        sys.stdout.flush()
    t1 = time.time()
    print "\t%5d\t%5d" % (minsize, t1 - t0)
