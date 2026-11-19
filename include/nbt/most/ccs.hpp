#ifndef NBT_MOST_CCS_HPP
#define NBT_MOST_CCS_HPP

#include "nbt/ccs.h"

namespace nbt {
namespace most {

class Ccs {
public:
    explicit Ccs(const nbt_ccs_config *config = NULL);

    static void defaults(nbt_ccs_config *config);
    static bool valid(const nbt_ccs_config *config);
    void reset();
    size_t feed(unsigned fblock, unsigned inst, unsigned fkt, unsigned op,
                const unsigned char *data, size_t length, nbt_input *out, size_t max);
    const nbt_ccs_config &config() const {
        return state_.config;
    }
    bool backHeld() const;
    bool backKeyHeld() const;

private:
    nbt_ccs state_;
};

} // namespace most
} // namespace nbt

#endif
