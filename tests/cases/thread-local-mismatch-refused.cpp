// [dcl.stc]/4: thread_local is written on every declaration of the object or
// on none - a static member declared thread_local and defined without it is
// two different kinds of storage for one name. clang's words.
struct Holder {
    static thread_local int shared;
};
int Holder::shared = 1;
int main() { return Holder::shared; }
