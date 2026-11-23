#pragma once
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
namespace synergy {
enum class Locale { En, FrCA, Unknown };
using TrArgs = std::initializer_list<std::pair<std::string_view, std::string>>;
Locale locale_from_string(std::string_view value);
const char* locale_string(Locale locale);
const char* locale_name(Locale locale);
std::string tr(std::string_view key, Locale locale, TrArgs args = {});
size_t catalog_size();
bool catalog_valid();
}
