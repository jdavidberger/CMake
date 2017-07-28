from __future__ import print_function
import sys, cmakelib, json, os, shutil

debug = True

cmakeCommand = sys.argv[1]
testFile = sys.argv[2]
sourceDir = sys.argv[3]
buildDir = sys.argv[4] + "/" + os.path.splitext(os.path.basename(testFile))[0]
cmakeGenerator = sys.argv[5]

print("Debugger Test:", testFile,
      "\n-- SourceDir:", sourceDir,
      "\n-- BuildDir:", buildDir,
      "\n-- Generator:", cmakeGenerator)

if os.path.exists(buildDir):
    shutil.rmtree(buildDir)

filterBase = sourceDir


def filterPacket(msg):
    # We attach PID of the process as a mechanism
    # for clients to attach to multiple servers
    # and track them. Strip that so our test cases match
    if 'PID' in msg:
        del msg['PID']
    if 'Backtrace' in msg:
        for k in msg['Backtrace']:
            # Make all backtrace paths relative
            k['File'] = k['File'].replace(filterBase, '')
    if 'State' in msg:
        # We filter out all running messages. Running messages
        # are never guaranteed to come through -- if the process
        # finishes or breaks before the internal message is
        # broadcast, it just sends the current state
        if msg['State'] == "Running":
            cmakelib.printServer("(ignored)", msg)
            return None
    return msg
cmakelib.filterPacket = filterPacket

with open(testFile) as f:
    testData = json.loads(f.read())

for communicationMethod in cmakelib.communicationMethods:
    proc = cmakelib.initDebuggerProc(cmakeCommand, buildDir, sourceDir + "/buildsystem1", communicationMethod)

    for obj in testData:
        if cmakelib.handleBasicMessage(proc, obj, debug):
            pass
        elif 'waitForPause' in obj:
            print("WAIT_FOR_PAUSE:")
            while True:
                packet = cmakelib.waitForRawMessage(proc)
                if packet is None:
                    print("Connection closed while waiting for pause")
                    sys.exit(-1)
                if 'State' in packet and packet['State'] == "Paused":
                    break
        else:
            print("Unknown command:", json.dumps(obj))
            sys.exit(2)

    print("Completed")

    cmakelib.shutdownProc(proc)
