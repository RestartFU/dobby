#include "diagnostics/client_schema_trace.hpp"

#include <array>
#include <cstddef>

namespace dobby {
namespace {

constexpr std::size_t kMaximumSchemaDepth = 32;
constexpr std::size_t kMaximumMemberLength = 128;
constexpr std::size_t kMaximumPathLength = 512;

struct ClientSchemaContext {
    std::array<std::string, kMaximumSchemaDepth> components;
    std::size_t depth{};
};

thread_local ClientSchemaContext context;

void pushComponent(std::string component) {
    if (context.depth >= context.components.size())
        return;
    context.components[context.depth++] = std::move(component);
}

} // namespace

void pushClientSchemaMember(std::string_view name) {
    pushComponent(std::string(name.substr(0, kMaximumMemberLength)));
}

void pushClientSchemaElement(std::uint64_t index) {
    pushComponent("[" + std::to_string(index) + "]");
}

void popClientSchemaContext() {
    if (context.depth == 0)
        return;
    context.components[--context.depth].clear();
}

std::string currentClientSchemaPath() {
    std::string result;
    for (std::size_t index = 0; index < context.depth; ++index) {
        const auto& component = context.components[index];
        if (component.empty())
            continue;
        if (!result.empty() && component.front() != '[')
            result += '.';
        if (result.size() + component.size() > kMaximumPathLength)
            break;
        result += component;
    }
    return result;
}

void clearClientSchemaTrace() {
    context = {};
}

} // namespace dobby
