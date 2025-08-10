// covering all cases for different data types

int main (int argc,char** argv) {

    // int, long and int pointer
    int i1 = 042;
    int* ip = &i1;
    long l1 = 123456789;

    // double
    double d1 = 3.14;
    double d2 = .5;
    double d3 = 1.;
    
    // float
    float f1 = 123f;
    float f2 = .25F;
    float pf = 1.f;
    float* pf_ptr = nullptr;

    // char
    char c1 = 'a';
    char c3 = '\n';

    // string
    char* s1 = "Hello";
    char* s2 = "line1\nline2";

    // bool
    bool b1 = true;
    bool b2 = false;

    // arrays
    int arr1[5] = {1, 2, 3, 4, 5};
    int arr2[3][2] = {{1, 2}, {3, 4}, {5, 6}};
    int* arr_ptr = arr1;

    int number;
    printf("Enter number: ");
    scanf("%d", &number);

}