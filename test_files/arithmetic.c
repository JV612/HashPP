int main()
{
    int a = 5, b = 2, c;
    c = a + b;
    c = a - b;
    c = a * b;
    c = a / b;
    c = a % b;

    a + b + c;
    10 / 0;
    10 % 0;

    a = 5.5;
    a = 5.5 + 2.1;

    double f1 = 1.5, f2 = 2.5, f3;
    f3 = f1 + f2;
    f3 = f1 - f2;
    f3 = f1 * f2;
    f3 = f1 / f2;
    f3 = f1 % f2;     //ERROR

    double d = 5.0;
    d = f1 + d;
    f1 = f1 + a;
    d = d + f1;

    c = a + f1;
    f3 = f1 + a;
    d = d + a;
    a = d + f1;
    f3 = 2 + 3.5;

    c = +a;
    c = -a;
    f3 = -f1;
    c = +5.5;
    c = ~a;
    c = ~f1;    //ERROR

    c = a + b * c;
    f3 = (f1 + f2) / 2;
    a = (a + b) % c;
    c = a / (b - 2);

    char ch = 'A';
    int arr[3];
    struct S
    {
        int x;
    } s;

    a = arr + 5; //ERROR
    c = a + s;  //ERROR
    c = a + &b; //ERROR
    c = a + "test"; //ERROR
    c = a * main;  //ERROR
    c = a / 'A';
    c = a * ch;

    a = a + b;
    a = a + 3.5;
    f1 = a + 2;
    f1 = f2 + 5;
    f1 = a % f2;  //ERROR
    d = f1 * 1.0;

    int k = 2 + 3 * 4;
    double f = (1 + 2.5) / 3;
    double g = 3.0 / 0.0;

    a = (a + f1) * (d - b);
    f1 = (f1 + f2) / a;

    a = ++a + b;
    b = a++ * 2;
    c = (a = 5) + (b = 10);
    c = a + (b = f1);

    a = 2147483647 + 1;
    c = (a / 0) + 2;
    return 0;
}