int main()
{
    int a = 5, b = 10, c = 0;
    double x = 2.5, y = 4.0;
    char *s = "test";
    int arr[3];
    struct S { int x; } st;

    if (a < b)
        a = b;
    if (x > y)
        y = x;
    if (a == b)
        c = 1;
    if (a != b)
        c = 2;
    if (a && b)
        c = 3;
    if (a || b)
        c = 4;

    if (x && y)
        a = 1;
    if (a && x)
        b = 2;
    if (s)
        c = 3;
    if (a < x)
        a = 4;
    if (x > a)
        b = 5;

    if (arr)          
        a = 1;
    if (st)           
        b = 2;        // ERROR
    if (1)         
        c = 3;        // ERROR
    if ("hello" && s)
        c = 4;

    if (a)
        b = 1;
    else
        b = 2;

    if (a > b)
        if (c)
            a = 10;
        else
            a = 20;
    else
        a = 30;

    switch (a)
    {
        case 1:
            b = 10;
            break;
        case 2:
            b = 20;
            break;
        case 3:
        case 4:
            b = 30;
            break;
        default:
            b = 40;
    }

    switch (3)
    {
        case 1:
            a = 1;
    }                 // ERROR

    while (a > 0)
    {
        a = a - 1;
        if (a == 2)
            continue;
        if (a == 1)
            break;
    }

    while (s)         
       a = 1;        // ERROR

    do
    {
        a = a + 1;
    } while (a < 5);

    for (a = 0; a < 5; a = a + 1)
        b = b + a;

    for (a = 0; a < 10; a++)
    {
        if (a == 5)
            continue;
        if (a == 8)
            break;
    }

    while (x < y)
    {
        x = x + 0.5;
    }

    do
        y = y - 1.0;
    while (y > 0);

    until(x>0)
    {
        x=x+3;
    }

    for (x = 0; x < 3.0; x = x + 1.0)
        y = y + x;
    return 0;
}
