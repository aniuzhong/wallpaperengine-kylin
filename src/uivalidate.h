#pragma once

#include <string>

// Pure predicates behind the local server's security boundary. That server
// is a control surface for the user's session, so every request has to clear
// them. Keeping them pure keeps them testable without a socket, and keeps
// the whole boundary in one auditable file instead of sprinkled through
// handlers.
namespace Ui {

// The cookie the session token travels in.
extern const char* const kSessionCookie;

// Constant-time comparison. A byte-by-byte early return leaks the token's
// prefix to anything that can time a request, and everything local can.
bool tokenMatches(const std::string& provided, const std::string& expected);

// Whether a Host header is one this server must answer to. Browsers send the
// port for non-default ports. Anything else — most importantly a DNS name an
// attacker controls that resolves to 127.0.0.1 — is a rebinding attempt: the
// browser would send the attacker's hostname in Host, and the attacker's
// page would then be same-origin with this server.
bool hostAllowed(const std::string& hostHeader, int port);

// The session token from a Cookie header ("a=1; wallpaper_engine_session=xyz; b=2"), or
// empty when absent. The token is deliberately not in the URL: a URL travels
// through argv, and /proc/<pid>/cmdline is readable by every user on the
// machine, while the browser's cookie store and /proc/<pid>/environ are not.
std::string sessionTokenFromCookie(const std::string& cookieHeader, const std::string& name);

// Whether a wallpaper id from the URL is a safe lookup key: a single path
// component, no separators, no traversal. Ids are matched against scanned
// directory names, so an escaping id could not resolve to anything anyway —
// this rejects it before the lookup and documents that the assumption is
// deliberate rather than accidental.
bool isSafeWallpaperId(const std::string& id);

} // namespace Ui
