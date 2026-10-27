#pragma once

#include <cstdint>

namespace tarkov_companion {

// The community-documented formula
// (https://escapefromtarkov.fandom.com/wiki/Trading#Fees):
//
//   fee = VO * Ti * 4^PO + VR * Tr * 4^PR
//   PO = log10(VO / VR), PR = log10(VR / VO)
//   the positive one of PO/PR is raised to the power 1.08
//
// VO is the item's base price, VR the requested price, Ti = Tr = 0.03.
// Intelligence Center level 3 discounts the fee by 30%.
//
// An estimate: the constants change between wipes
// (docs/troubleshooting.md#verdicts-look-wrong-after-a-wipe).
int64_t fleaListingFee(int64_t basePrice, int64_t listPrice, int intelligenceCenterLevel);

}  // namespace tarkov_companion
