#!/usr/bin/env python

import sys
import os
import subprocess

LIBS = [
    'd3d9.lib',
    'd3dx9.lib',
    'dinput8.lib',
    'dsound.lib',
    'dxguid.lib',
    'gdi32.lib',
    'glu32.lib',
    'kernel32.lib',
    'msvcrt_old.lib',
    'opengl32.lib',
    'user32.lib',
    'winmm.lib'
]

FIXED_OPTIONS = ['/COMPMODE:SLOW', '/ORDERTRIES:10000', '/HASHSIZE:300', '/HASHTRIES:1000', '/PROGRESSGUI', '/PRINT:IMPORTS', '/PRINT:MODELS', '/PRIORITY:IDLE', '/LIBPATH:C:\\Program Files (x86)\\Microsoft Visual Studio 8\\VC\\ATLMFC\\LIB;C:\\Program Files (x86)\\Microsoft Visual Studio 8\\VC\\LIB;C:\\Program Files (x86)\\Microsoft Visual Studio 8\\VC\\PlatformSDK\\lib;C:\\Program Files (x86)\\Microsoft Visual Studio 8\\SDK\\v2.0\\lib;C:\\Program Files (x86)\\Microsoft DirectX SDK (June 2008)\\Lib\\x86;']

VARYING_OPTIONS = [[], ['/TRANSFORM:CALLS'], ['/RANGE:opengl32', '/RANGE:d3dx9_38'], ['/TRANSFORM:CALLS', '/RANGE:opengl32', '/RANGE:d3dx9_38']]
VARYING_OPTION_NAMES = ['_', 'T', 'R', 'TR']

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
print "\t  min"

for test in tests:
    argi = test.rindex('\t')
    name = test[0:argi].strip()
    args = test[argi+1:].strip().split(' ')
    exefile = name+exefile_postfix+".exe"

    print test[0:argi],
    sys.stdout.flush()
    minsize = 99999
    for o in VARYING_OPTIONS:
        cmdline = [crinkler_exe, "/OUT:"+exefile] + FIXED_OPTIONS + o + args + LIBS
        rval = subprocess.call(cmdline, stdout=logfile)
        if rval == 0:
            size = os.stat(exefile).st_size
            minsize = min(size,minsize)
            print "\t%5d" % size,
        else:
            print "\terror",
        sys.stdout.flush()
    print "\t%5d" % minsize
