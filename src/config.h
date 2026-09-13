#pragma once

#include <map>
#include <string>
#include <vector>

// Minimal INI reader. Sections are case-sensitive; keys are case-sensitive.
// Supports ";" and "#" comments and "[section]" headers.
class Config {
public:
    // Loads and replaces the current contents. Returns false only if the file
    // could not be opened (an empty config is still a "success" otherwise).
    bool Load(const std::wstring& path);

    std::string Get(const std::string& section, const std::string& key,
                    const std::string& fallback = "") const;
    int GetInt(const std::string& section, const std::string& key, int fallback) const;
    float GetFloat(const std::string& section, const std::string& key, float fallback) const;

    bool Has(const std::string& section, const std::string& key) const;

    // All section names, sorted (std::map ordering), for deterministic scanning.
    std::vector<std::string> Sections() const;

private:
    std::map<std::string, std::map<std::string, std::string>> data_;
};
