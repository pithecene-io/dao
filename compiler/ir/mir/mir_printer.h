#ifndef DAO_IR_MIR_MIR_PRINTER_H
#define DAO_IR_MIR_MIR_PRINTER_H

#include "ir/mir/mir.h"

#include <ostream>

namespace dao {

/// Print a human-readable MIR dump to the given stream.
/// Output is deterministic and suitable for golden-file testing.
void print_mir(std::ostream& out, const MirModule& module);

} // namespace dao

#endif // DAO_IR_MIR_MIR_PRINTER_H
