#pragma once

#include <chrono>
#include <string>
#include <vector>

// Running child processes: the three shapes this product needs — capture
// with a deadline (the engine's non-rendering modes), run to completion (a
// tool whose result we ignore), and fire-and-detach (a desktop that must
// outlive us). No shell is involved: argv goes to execvp verbatim.
namespace process {

// Run |program| with |args|, capturing stdout and stderr (interleaved in
// arrival order) into |output|, bounded by |timeout| — the child is
// SIGKILLed past the deadline so a hung GL init cannot wedge the caller.
// Returns the child's exit code, or -1 when it could not be spawned or was
// killed; |timedOut| distinguishes those two from a real exit code of -1.
int RunCaptured(const std::string& program, const std::vector<std::string>& args,
                std::chrono::milliseconds timeout, std::string* output, bool* timedOut);

// True when |exitCode| from RunCaptured means the program never produced a
// result: it was killed on the deadline, could not be spawned, or was not
// executable (127 is execvp's own failure code). Callers turn this into
// "no answer" rather than "the program answered with an error".
bool DidNotRun(int exitCode);

// Run |argv| to completion and return its exit code (-1 when it could not
// be spawned). Output goes to this process's own stdout/stderr; the result
// is the caller's to ignore.
int RunAndWait(const std::vector<std::string>& argv);

// Start |argv| and walk away. The double fork matters twice over: the new
// process must outlive the caller (a relaunched desktop), and it must
// never become the caller's zombie. False when |argv| is empty or its
// program cannot be executed at all.
bool SpawnDetached(const std::vector<std::string>& argv);

} // namespace process
