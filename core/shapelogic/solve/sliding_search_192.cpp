// SPDX-License-Identifier: GPL-3.0-or-later
// The slide search in 192-bit keys (BigKey<3>), compiled apart from the
// other widths so that the five build side by side (cleanup item 21.1).
#include "shapelogic/solve/slide_level_search.h"

namespace bt::sliding::detail {

template Tabled tableSearch<BigKey<3>>(const SlideBoard&, const TableForm&, bool, SlideSearch&,
                                 std::vector<SlideState>&);
template Tabled tableProbe<BigKey<3>>(const SlideBoard&, const TableForm&, bool, const SlideSearch&);
template std::optional<Tabled> nameSpace<BigKey<3>>(const SlideBoard&, const TableForm&,
                                              NamedSpace<BigKey<3>>&);

}  // namespace bt::sliding::detail
