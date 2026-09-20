#pragma once

#include <string>
#include <vector>

// Running the engine binary as a child process. The engine is a GL
// application that also has useful non-rendering modes (--list-properties),
// and both frontends need them with the same privileges and the same
// deadline, so the plumbing lives here rather than in a frontend.
namespace EngineProcess {

// Run |enginePath| with |args|, capturing stdout and stderr (interleaved in
// arrival order) into |output|, bounded by |timeoutMs| — the child is
// SIGKILLed past the deadline so a hung GL init cannot wedge the caller.
// Returns the child's exit code, or -1 when it could not be spawned or was
// killed; |timedOut| distinguishes those two from a real exit code of -1.
int runCaptured(const std::string& enginePath, const std::vector<std::string>& args, long timeoutMs,
                std::string* output, bool* timedOut);

// True when |exitCode| from runCaptured means the engine never produced a
// result: it was killed on the deadline, could not be spawned, or was not
// executable (127 is execvp's own failure code). Callers turn this into
// "no answer" rather than "the engine answered with an error".
bool didNotRun(int exitCode);

} // namespace EngineProcess
