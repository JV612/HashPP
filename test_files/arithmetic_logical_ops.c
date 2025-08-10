int main() {

    int a = 10, b = 5, c;
    
    // Arithmetic operators
    c = a + b;
    c = a - b;
    c = a * b;
    c = a / b;
    c = a % b;
    a++;
    b--;
    
    // Logical operators
    bool result = (a > b) && (c < 20);
    result = (a == b) || (c != 15);
    result = !(a >= b);
    result = a <= b;
    
    // Bitwise operators
    int bitwise_and = a & b;
    int bitwise_or = a | b;
    int bitwise_xor = a ^ b;
    int left_shift = a << 1;
    int right_shift = a >> 1;
    int bitwise_not = ~a;

    // Assignment operators
    a += b;
    a -= b;
    a *= b;
    a /= b;
    a %= b;
    a &= b;
    a |= b;
    a ^= b;
    a <<= 1;
    a >>= 1;

    // Conditional (ternary) operator
    int max = (a > b) ? a : b;

}