#include "uivalidate.h"

#include <cctype>
#include <cstddef>

namespace Ui {

const char* const kSessionCookie = "lwe_session";

bool tokenMatches(const std::string& provided, const std::string& expected) {
    if (expected.empty())
        return false;

    // Fold a length mismatch into the result instead of returning early, and
    // always run the full loop: the time taken must not depend on how much of
    // the token was guessed right.
    unsigned char difference = provided.size() == expected.size() ? 0 : 1;
    for (size_t i = 0; i < expected.size(); i++) {
        const unsigned char given =
            i < provided.size() ? static_cast<unsigned char>(provided[i]) : static_cast<unsigned char>(0);
        difference |= static_cast<unsigned char>(given ^ static_cast<unsigned char>(expected[i]));
    }
    return difference == 0;
}

bool hostAllowed(const std::string& hostHeader, int port) {
    if (hostHeader.empty())
        return false;
    const std::string withPort = ":" + std::to_string(port);
    for (const char* name : { "127.0.0.1", "localhost", "[::1]" }) {
        const std::string host = name;
        if (hostHeader == host || hostHeader == host + withPort)
            return true;
    }
    return false;
}

std::string sessionTokenFromCookie(const std::string& cookieHeader, const std::string& name) {
    size_t start = 0;
    while (start <= cookieHeader.size()) {
        const size_t end = cookieHeader.find(';', start);
        const size_t stop = end == std::string::npos ? cookieHeader.size() : end;
        std::string field = cookieHeader.substr(start, stop - start);

        // " name=value" — browsers put a space after each separator
        size_t begin = 0;
        while (begin < field.size() && std::isspace(static_cast<unsigned char>(field[begin])))
            begin++;
        field.erase(0, begin);

        const size_t equals = field.find('=');
        if (equals != std::string::npos && field.compare(0, equals, name) == 0)
            return field.substr(equals + 1);

        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return {};
}

bool isSafeWallpaperId(const std::string& id) {
    if (id.empty() || id.size() > 64)
        return false;
    if (id == "." || id == "..")
        return false;
    for (const char c : id) {
        if (c == '/' || c == '\\' || c == '\0' || c == '\n' || c == '\r')
            return false;
    }
    return true;
}

} // namespace Ui
