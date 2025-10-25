int main()
{
    int a;
    double d;
    char ch;
    char str[20];

    a = scan_int();
    d = scan_double();
    ch = scan_char();
    scan_string(str);

    print_int(a);
    print_double(d);
    print_char(ch);
    print_string(str);
    print_newline();

    a = a + 10;
    d = d * 2.5;
    ch = 'A';
    str[0] = 'H';
    str[1] = 'i';
    str[2] = '\0';

    print_int(a);
    print_double(d);
    print_char(ch);
    print_string(str);
    print_newline();

    if (a > 0)
    {
        print_string("Positive\n");
    }
    else
    {
        print_string("Non-positive\n");
    }

    for (int i = 0; i < 3; i++)
    {
        print_int(i);
        print_newline();
    }

    while (d > 0)
    {
        d = d - 1.0;
        print_double(d);
        print_newline();
    }

    return 0;
}
