// [class.friend]/3: a qualified friend class names a class already declared; n has no Missing.
namespace n { struct Here; }
class Box {
    friend class n::Missing;
    int secret;
};
int main() { return 0; }
