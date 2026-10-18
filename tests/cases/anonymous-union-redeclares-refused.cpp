struct S { int a; union { int a; double b; }; };
// The union's members are the class's, so a name already there is redeclared - at the union member.
