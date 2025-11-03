#ifndef WINFILEDIALOG_H_INCLUDED
#define WINFILEDIALOG_H_INCLUDED

#pragma once
#include <string>
#include <optional>

std::optional<std::string> OpenPrgFileDialog();
std::optional<std::string> OpenCartFileDialog();

#endif // WINFILEDIALOG_H_INCLUDED
