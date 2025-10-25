int main()
{
    int a = 5, b = 10, c;
    int arr[3];
    int arr2[2][2];
    double d = 2.5;
    char *s = "hello";
    int *p;
    int *q;
    double *dp;
    char **sp;
    int *arrp[3];

    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;

    a = arr[0];
    b = arr[1] + arr[2];
    arr[a] = b;
    arr[a + 1] = 5;

    p = &a;
    q = &b;
    c = *p + *q;
    *p = 20;
    a = *q;

    p = arr;
    c = *(p + 1);
    *(arr + 2) = 15;

    dp = &d;
    d = *dp + 1.0;
    *dp = 3.14;

    sp = &s;
    s = *sp;

    a = *s;            // ERROR
    p = d;             // ERROR
    dp = &a;           // ERROR
    arr = p;           // ERROR
    q = arr;           // ERROR
    arr[1.5] = 10;     // ERROR
    arr[a] = s;        // ERROR
    *arr = &a;         // ERROR
    p = &arr;          // ERROR
    *p = arr;          // ERROR

    arrp[0] = &a;
    arrp[1] = &b;
    *arrp[0] = 50;
    **arrp = 100;      // ERROR

    int *r;
    r = &arr[1];
    a = *r;
    r = r + 1;
    a = *(r - 1);

    char *t = "test";
    t = t + 1;
    s = s + 2;
    *t = 'A';          // ERROR (string literal modification)
    return 0;
}
