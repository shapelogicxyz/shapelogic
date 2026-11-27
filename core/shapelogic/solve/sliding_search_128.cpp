// SPDX-License-Identifier: GPL-3.0-or-later
// The slide search in 128-bit keys (Key128), compiled apart from the
// other widths so that the five build side by side (cleanup item 21.1).
#include "shapelogic/solve/slide_level_search.h"

namespace bt::sliding::detail {

template Tabled tableSearch<Key128>(const SlideBoard&, const TableForm&, bool, SlideSearch&,
                                 std::vector<SlideState>&);
template Tabled tableProbe<Key128>(const SlideBoard&, const TableForm&, bool, const SlideSearch&);
template std::optional<Tabled> nameSpace<Key128>(const SlideBoard&, const TableForm&,
                                              NamedSpace<Key128>&);

}  // namespace bt::sliding::detail
