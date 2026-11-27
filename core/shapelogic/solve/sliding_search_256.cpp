// SPDX-License-Identifier: GPL-3.0-or-later
// The slide search in 256-bit keys (BigKey<4>), compiled apart from the
// other widths so that the five build side by side (cleanup item 21.1).
#include "shapelogic/solve/slide_level_search.h"

namespace bt::sliding::detail {

template Tabled tableSearch<BigKey<4>>(const SlideBoard&, const TableForm&, bool, SlideSearch&,
                                 std::vector<SlideState>&);
template Tabled tableProbe<BigKey<4>>(const SlideBoard&, const TableForm&, bool, const SlideSearch&);
template std::optional<Tabled> nameSpace<BigKey<4>>(const SlideBoard&, const TableForm&,
                                              NamedSpace<BigKey<4>>&);

}  // namespace bt::sliding::detail
