struct Point {
    int x;
    int y;
};

struct Pair {
    double a;
    double b;
};

struct Nested {
    struct Point p;
    int val;
};

union Data {
    int i;
    double d;
    char *s;
};

enum Color {
    RED,
    GREEN,
    BLUE
};

int main()
{
    struct Point p1;
    struct Pair pr;
    struct Nested n;
    union Data d1;
    enum Color c1, c2;

    p1.x = 10;
    p1.y = 20;
    pr.a = 2.5;
    pr.b = 3.5;
    n.p = p1;
    n.val = 5;

    d1.i = 5;
    d1.d = 2.5;
    d1.s = "hello";

    c1 = RED;
    c2 = GREEN;

    int x = p1.x + p1.y;
    double y = pr.a * pr.b;

    p1 = pr;            // ERROR (incompatible struct types)
    n = p1;             // ERROR
    d1 = p1;            // ERROR
    c1 = 2.5;        
    c1 = BLUE;
    c1 = 10;            
    c2 = c1;

    p1.z = 5;           // ERROR (no such member)
    p1.x = d1;          // ERROR
    p1 = d1;            // ERROR

    d1.i = 100;
    int val = d1.i;
    double f = d1.d;

    d1.x = 5;           // ERROR (no such union member)
    d1.i = p1.x + p1.y;

    struct Point p2;
    struct Point p3;

    struct Unknown u;   // ERROR (undefined struct)
    enum Unused e;      // ERROR (undefined enum)
    union NotDef v;     // ERROR (undefined union)

    struct Point arr[2];
    arr[0] = p1;
    arr[1] = p2;
    arr[0].x = 50;
    arr[1].y = arr[0].x;

    struct Point *ptr = &p1;
    ptr->x = 10;
    (*ptr).y = 20;
    ptr = &p2;
    ptr = arr;
    p1 = *ptr;
    ptr = &pr;          // ERROR (different struct type)
    *ptr = pr;          // ERROR

    union Data d2;
    d2 = d1;
    d2.s = "abc";
    d2.d = 1.2;
    d2.i = 42;
    n.val = d2.i;

    enum Color c3 = BLUE;
    c3 = 1;            
    c3 = c1;

    struct Nested m;
    m.p.x = 7;
    m.p.y = 8;
    m.val = 100;
    m.p = p1;
    m = n;
    return 0;
}
