// Expected to fail: built without -freflection, the reflection header refuses to compile
// instead of falling back to another code path.
#include "tep/reflect.hpp"
int main() {}
