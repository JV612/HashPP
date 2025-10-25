static int g1 = 10;
static double g2 = 2.5;
int g3 = 5;

static void helper(int a)
{
    static int counter = 0;
    counter = counter + a;
}

void normalFunc()
{
    static int callCount = 0;
    callCount++;
}

static int addStatic(int a, int b)
{
    return a + b;
}

int main()
{
    static int a = 5;
    static double d = 3.5;
    int b = 10;

    a = a + b;
    d = d + g2;

    static int arr[3] = {1, 2, 3};
    static int x; 
    x = arr[1];

    static char *msg = "hello";
    msg = "world";

    helper(a);
    normalFunc();
    int sum = addStatic(a, b);

    //static int a = 10;        // ERROR (duplicate definition in same scope)

    static double *p;
    p = &d;

    if (a > 0)
    {
        static int nested = 1;
        nested++;
    }

    for (int i = 0; i < 3; i++)
    {
        static int loopCount = 0;
        loopCount += i;
    }

    static int *ptrArr[2];
    ptrArr[0] = &a;
    ptrArr[1] = &b;
    return 0;
}
