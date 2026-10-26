// [class.mem]/4 and [basic.lookup.unqual]/7: a default member initialiser is read in the scope of the class, whose
// enclosing namespaces are searched after it - so `kNone` here is n::kNone. cxx3 reads the initialiser with no
// namespace open and says 'kNone' was not declared; written `n::kNone`, or in a constructor's mem-initialiser, it is
// found. Found by the self-host of M3a.5: OptValues.cpp's `Cond cc = kNoCond;` was the shape, written qualified since.
extern "C" int printf(const char *, ...);
namespace n {
enum E { kNone = -1, kSome };
struct V {
    E e = kNone;
    int k = kSome;
};
}
int main() { n::V v; printf("%d %d\n", v.e, v.k); return 0; }
