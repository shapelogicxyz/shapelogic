// SPDX-License-Identifier: GPL-3.0-or-later
// The slide search in 64-bit keys (std::uint64_t), compiled apart from the
// other widths so that the five build side by side (cleanup item 21.1).
#include "shapelogic/solve/slide_level_search.h"

namespace sl::sliding::detail {

template Tabled tableSearch<std::uint64_t>(const SlideBoard&, const TableForm&, bool, SlideSearch&,
                                 std::vector<SlideState>&);
template Tabled tableProbe<std::uint64_t>(const SlideBoard&, const TableForm&, bool, const SlideSearch&);
template std::optional<Tabled> nameSpace<std::uint64_t>(const SlideBoard&, const TableForm&,
                                              NamedSpace<std::uint64_t>&);

}  // namespace sl::sliding::detail
