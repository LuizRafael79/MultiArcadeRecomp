#include "naomi_jvs_837_13551.hpp"

#include "naomi_holly.hpp"

namespace mamerecomp::naomi {

MapleEndpoint make_jvs_maple_endpoint(NaomiJvs13551& board) {
    return [&board](unsigned bus, std::span<const std::uint32_t> request) {
        return board.mapleDmaEndpoint(bus, request);
    };
}

} // namespace mamerecomp::naomi
