// SPDX-License-Identifier: GPL-3.0-or-later
// The slide search in 512-bit keys (BigKey<8>), compiled apart from the
// other widths so that the five build side by side (cleanup item 21.1).
#include "shapelogic/solve/slide_level_search.h"

namespace sl::sliding::detail {

template Tabled tableSearch<BigKey<8>>(const SlideBoard&, const TableForm&, bool, SlideSearch&,
                                 std::vector<SlideState>&);
template Tabled tableProbe<BigKey<8>>(const SlideBoard&, const TableForm&, bool, const SlideSearch&);
template std::optional<Tabled> nameSpace<BigKey<8>>(const SlideBoard&, const TableForm&,
                                              NamedSpace<BigKey<8>>&);

}  // namespace sl::sliding::detail
