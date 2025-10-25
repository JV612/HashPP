class MyClass
{
    int a;
    double d;
    char* name;

    MyClass(int val, double f, char* n)   // constructor
    {
        a = val;
        d = f;
        name = n;
    }

    ~MyClass()                             // destructor
    {
        // cleanup if needed
    }

    void setValues(int val, double f, char* n)
    {
        a = val;
        d = f;
        name = n;
    }

    int getA()
    {
        return a;
    }

    double getD()
    {
        return d;
    }
};

int main()
{
    class MyClass obj1(10, 2.5, "Alice");
    class MyClass obj2(20, 3.5, "Bob");

    int valA = obj1.getA();
    double valD = obj2.getD();

    obj1.setValues(30, 4.5, "Charlie");

    obj1.a = 50;
    obj1.d = 6.7;
    obj1.name = "David";

    obj1.a = obj2;         // ERROR (cannot assign object to int)
    obj1.d = obj2;         // ERROR (cannot assign object to double)
    obj1.name = obj2;      // ERROR (cannot assign object to char*)

    class MyClass obj4(1, 2.0, "Eve");
    obj4 = obj2;           // valid (copy assignment)
    return 0;
}
