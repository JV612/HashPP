int add(int x, int y)
{
    return x + y;
}

double mul(double a, double b)
{
    return a * b;
}

void printInt(int x)
{
    int a = x;
}

int retInt()
{
    return 10;
}

double retDouble()
{
    return 2.5;
}

void noReturn()
{
    int a = 5;
}

int invalidReturn()
{
    double x = 3.5;
    return x;       // valid implicit conversion
}

int main()
{
    int a = 5, b = 10, c;
    double x = 2.5, y = 4.0, z;
    char *s = "abc";

    c = add(a, b);
    z = mul(x, y);
    printInt(a);
    a = retInt();
    x = retDouble();

    c = add(a);             // ERROR (too few args)
    c = add(a, b, c);       // ERROR (too many args)
    c = add(x, y);          // ERROR (wrong argument types)
    x = mul(a, b);          // valid (int promoted to double)
    add();                  // ERROR
    printInt();             // ERROR
    printInt(a, b);         // ERROR
    z = retInt();           // valid (int → double conversion)
    a = retDouble();        // valid (double → int truncation)

    noReturn();
    a = noReturn();         // ERROR
    z = noReturn();         // ERROR

    int p;
    p = main;               // ERROR
    p = add;                // ERROR
    c = add;                // ERROR
    a = add(a, b) + mul(x, y);
    add(a, b) = 5;          // ERROR
    mul(a, b) = 10.0;       // ERROR

    return 0;
}
