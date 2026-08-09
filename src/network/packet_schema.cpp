#include "network/packet_schema.hpp"

namespace dobby {

std::string_view packetWireSchema(std::int32_t packetId) {
    switch (packetId) {
    case 49:
        return "InventoryContent: container_id -> item_count -> repeated NetworkItemStackDescriptor";
    case 50:
        return "InventorySlot: container_id -> slot -> optional full_container_name -> "
               "optional storage_item -> NetworkItemStackDescriptor item";
    case 51:
        return "ContainerSetData: container_id -> property -> value";
    case 58:
        return "FullChunkData: dimension/chunk coordinates -> subchunk data -> block entities -> cache data";
    case 156:
        return "PacketViolationWarning: violation_type -> severity -> packet_id -> context string";
    default:
        return "Schema fields are not catalogued yet; the read trace still shows byte-level decode progress.";
    }
}

} // namespace dobby
