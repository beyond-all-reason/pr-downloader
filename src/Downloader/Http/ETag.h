#pragma once

#include <string>
#include <optional>

std::optional<std::string> getETag(const std::string& file);
void setETag(const std::string& file, const std::string& value);
