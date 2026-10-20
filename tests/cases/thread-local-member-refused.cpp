// [dcl.stc]/4: thread_local goes on a variable of namespace or block scope or
// on a static data member, and on nothing else - clang's words for a
// non-static member.
struct Holder {
    thread_local int perObject;
};
int main() { return 0; }
