#!/usr/bin/env python

import runtests
import sys
import subprocess

CRINKLERS = [
    "crinkler04",
    "crinkler10a", "crinkler11", "crinkler12", "crinkler13", "crinkler14",
    "crinkler20a", "crinkler21a/win64", "crinkler22/win64", "crinkler23/win64"
]

OPTIONS = ['/COMPMODE:FAST']

if __name__ == "__main__":
    recompress_crinkler_exe = sys.argv[1]
    testlist = sys.argv[2]
    chosen = sys.argv[3:]

    logfile = open("recomplog.txt", "w")

    for orig_crinkler in CRINKLERS:
        orig_crinkler_exe = "../releases/" + orig_crinkler + "/crinkler.exe"
        version = orig_crinkler[8:10]
        print("Crinkler " + version[0] + "." + version[1])
        makename = lambda x: "orig" + version + "_" + x
        def post(name):
            filename = "recomp" + version + "_" + name
            filename2 = "recompr" + version + "_" + name

            recomp_cmdline = [recompress_crinkler_exe,
                "/RECOMPRESS", makename(name) + ".exe",
                "/OUT:" + filename + ".exe",
                "/REPORT:" + filename + ".html"
            ]
            rval = subprocess.call(recomp_cmdline, stdout=logfile)

            recomp_cmdline2 = [recompress_crinkler_exe,
                "/RECOMPRESS", filename + ".exe",
                "/OUT:" + filename2 + ".exe",
                "/REPORT:" + filename2 + ".html"
            ]
            rval = subprocess.call(recomp_cmdline2, stdout=logfile)

        runtests.runtests(orig_crinkler_exe, testlist, logfile, chosen, makename, OPTIONS, report=False, post=post)
