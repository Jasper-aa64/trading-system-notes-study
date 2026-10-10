// #13 轮讲：源 :68-137 的 Mixin 代码（逐字），加两种组合顺序的对照。
//   g++ -std=c++20 -O2 -Wall -Wextra 13-mixin-check.cpp && ./a.out
// GCC 13.3 输出：DoubleValue<OddOnly<MatrixWalk>>: 2 6 10；OddOnly<DoubleValue<MatrixWalk>>: 空（翻倍后全是偶数）。
#include <span>
#include <utility>
#include <vector>

struct Cell {
    int row{};
    int col{};
    int value{};
};

// 基础层：按行主序遍历二维vector
class MatrixWalk {
public:
    explicit MatrixWalk(std::span<const std::vector<int>> rows) : rows_{rows} { skip_empty(); }

    [[nodiscard]] Cell cell() const {
        return {.row = static_cast<int>(r_), .col = static_cast<int>(c_), .value = rows_[r_][c_]};
    }
    [[nodiscard]] bool valid() const noexcept { return r_ < rows_.size(); }

    void next() {
        if (!valid()) return;
        ++c_;
        if (c_ >= rows_[r_].size()) {
            c_ = 0;
            ++r_;
            skip_empty();
        }
    }

private:
    std::span<const std::vector<int>> rows_;
    std::size_t r_{};
    std::size_t c_{};

    void skip_empty() {
        while (r_ < rows_.size() && rows_[r_].empty()) ++r_;
    }
};

// Mixin层：只保留奇数
template <class Base>
class OddOnly : public Base {
public:
    template <class... Args>
    explicit OddOnly(Args&&... args) : Base(std::forward<Args>(args)...) {
        while (Base::valid() && (Base::cell().value % 2 == 0)) Base::next();
    }

    void next() {
        do {
            Base::next();
        } while (Base::valid() && (Base::cell().value % 2 == 0));
    }
};

// Mixin层：值翻倍
template <class Base>
class DoubleValue : public Base {
public:
    template <class... Args>
    explicit DoubleValue(Args&&... args) : Base(std::forward<Args>(args)...) {}

    [[nodiscard]] Cell cell() const {
        Cell x = Base::cell();
        x.value *= 2;
        return x;
    }
};

#include <cstdio>
template <class It> void dump(const char* name, It it) {
    std::printf("%s:", name);
    for (; it.valid(); it.next()) std::printf(" %d", it.cell().value);
    std::printf("\n");
}
int main() {
    std::vector<std::vector<int>> matrix{{1, 2, 3}, {}, {4, 5, 6}};
    std::span<const std::vector<int>> rows(matrix);
    dump("DoubleValue<OddOnly<MatrixWalk>>", DoubleValue<OddOnly<MatrixWalk>>{rows});
    dump("OddOnly<DoubleValue<MatrixWalk>>", OddOnly<DoubleValue<MatrixWalk>>{rows});
}
