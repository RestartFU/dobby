#pragma once

#include "diagnostics/types.hpp"

#include <optional>
#include <string>

namespace dobby {

std::optional<InferredDivergence>
inferDivergence(std::int32_t packetId, const StreamFailure& failure);
std::string formatInferredDivergence(const InferredDivergence& inference);
std::string summarizeInferredDivergence(const InferredDivergence& inference);

} // namespace dobby
