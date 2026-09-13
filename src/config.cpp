#include "config.h"

#include <cstdlib>
#include <fstream>

namespace {

std::string Trim(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const size_t first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return "";
    }
    const size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

} // namespace

bool Config::Load(const std::wstring& path) {
    data_.clear();

    std::ifstream file(path.c_str());
    if (!file) {
        return false;
    }

    std::string line;
    std::string section;

    while (std::getline(file, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = Trim(trimmed.substr(1, trimmed.size() - 2));
            continue;
        }

        const size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = Trim(trimmed.substr(0, equals));
        const std::string value = Trim(trimmed.substr(equals + 1));
        if (!key.empty()) {
            data_[section][key] = value;
        }
    }

    return true;
}

std::string Config::Get(const std::string& section, const std::string& key,
                        const std::string& fallback) const {
    const auto sectionIt = data_.find(section);
    if (sectionIt == data_.end()) {
        return fallback;
    }
    const auto keyIt = sectionIt->second.find(key);
    if (keyIt == sectionIt->second.end()) {
        return fallback;
    }
    return keyIt->second;
}

int Config::GetInt(const std::string& section, const std::string& key, int fallback) const {
    const std::string value = Get(section, key);
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 0); // base 0: allows 0x..
    if (end == value.c_str() || *end != '\0') {
        return fallback;
    }
    return static_cast<int>(parsed);
}

float Config::GetFloat(const std::string& section, const std::string& key, float fallback) const {
    const std::string value = Get(section, key);
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') {
        return fallback;
    }
    return parsed;
}

bool Config::Has(const std::string& section, const std::string& key) const {
    const auto sectionIt = data_.find(section);
    if (sectionIt == data_.end()) {
        return false;
    }
    return sectionIt->second.find(key) != sectionIt->second.end();
}

std::vector<std::string> Config::Sections() const {
    std::vector<std::string> sections;
    sections.reserve(data_.size());
    for (const auto& entry : data_) {
        sections.push_back(entry.first);
    }
    return sections;
}
