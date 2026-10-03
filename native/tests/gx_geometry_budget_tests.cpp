#include "geometry_budget.hpp"
#include <cassert>
#include <limits>
int main() {
    using namespace PetariGeometry;
    assert(fitsGeometry(0, 0, VertexBytes, IndexBytes));
    assert(!fitsGeometry(0, 0, VertexBytes + 1, 0));
    assert(!fitsGeometry(0, 0, 0, IndexBytes + 1));
    assert(fits(7, 8, 16));
    assert(!fits(7, 9, 16));
    assert(!fits(std::numeric_limits< std::size_t >::max(), 1, IndexBytes));
    assert(!fits(1, std::numeric_limits< std::size_t >::max(), IndexBytes));
    assert(fitsGeometry(0, 2 * 1024 * 1024, 0, 20));
    std::size_t used = 0;
    unsigned accepted = 0, skipped = 0;
    for (unsigned i = 0; i < 20000; ++i) {
        if (fitsGeometry(used, used, 1024, 1024)) {
            used += 1024;
            ++accepted;
        } else
            ++skipped;
    }
    assert(accepted == 8192 && skipped == 11808 && used == IndexBytes);
    assert(fitsGeometry(0, 0, 1024, 1024));  // A new frame resumes normally.
    assert(stagingBytes(64 * 1024 * 1024) == 73 * 1024 * 1024);
}
