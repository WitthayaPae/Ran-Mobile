#pragma once
// TBB is server-side only; the client just needs blocked_range/parallel_for to
// parse. Serial fallbacks keep behaviour identical for the single call site.
namespace tbb {
template <class T> class blocked_range {
    T b, e, g;
public:
    blocked_range(T begin, T end, T grain = 1) : b(begin), e(end), g(grain) {}
    T begin() const { return b; }
    T end() const { return e; }
    T grainsize() const { return g; }
};
template <class Range, class Body> inline void parallel_for(const Range &r, const Body &body) { body(r); }
class task_scheduler_init { public: task_scheduler_init(int = 0) {} ~task_scheduler_init() {} };
}
