#pragma once

#include "config.h"

#include <QStringList>

// Pure mapping from Config to engine argv. Every flag emitted here
// corresponds to one engine CLI argument — the UI never invents options.
QStringList buildArgv(const Config& config);

// Human/Log form of the same argv, quoted for display purposes.
QString buildCommandLine(const Config& config);
